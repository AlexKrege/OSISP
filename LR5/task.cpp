#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <sstream>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>
#include <algorithm>

#define DEFAULT_PORT 20012
#define BUFFER_SIZE 1200

// Структура статистики сервера
struct ServerStats {
    uint32_t total_requests = 0;
    uint32_t unique_requests = 0;
    uint32_t duplicate_requests = 0;
    uint32_t time_cmds = 0;
    uint32_t echo_cmds = 0;
    uint32_t add_cmds = 0;
    uint32_t reverse_cmds = 0;
    uint32_t stats_cmds = 0;
};

// Функция для получения текущего времени в виде строки
std::string GetCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    ctime_s(buf, sizeof(buf), &now_c);
    std::string str(buf);
    if (!str.empty() && str.back() == '\n') str.pop_back();
    return str;
}

// Помощник для логирования с меткой времени
void Log(const std::string& role, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt;
    localtime_s(&bt, &timer);
    std::cout << "[" << std::put_time(&bt, "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
              << "[" << role << "] " << message << std::endl;
}

// Серверная часть
void RunServer(int port, double drop_rate, int student_id) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Log("SERVER", "WSAStartup failed. Error: " + std::to_string(WSAGetLastError()));
        return;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        Log("SERVER", "Socket creation failed. Error: " + std::to_string(WSAGetLastError()));
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        Log("SERVER", "Bind failed on port " + std::to_string(port) + ". Error: " + std::to_string(WSAGetLastError()));
        closesocket(serverSocket);
        WSACleanup();
        return;
    }

    Log("SERVER", "START port=" + std::to_string(port) + " drop_rate=" + std::to_string(drop_rate));

    ServerStats stats;
    std::map<uint32_t, std::string> responseCache;
    std::list<uint32_t> cacheKeys;

    std::mt19937 rng(1000 + student_id * 97);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    char buffer[BUFFER_SIZE];

    while (true) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE - 1, 0, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (bytesReceived == SOCKET_ERROR) {
            Log("SERVER", "recvfrom failed. Error: " + std::to_string(WSAGetLastError()));
            continue;
        }
        buffer[bytesReceived] = '\0';
        std::string requestStr(buffer);

        uint32_t reqId = 0;
        std::string cmd = "";
        std::string arg = "";

        size_t idPos = requestStr.find("REQ_ID|");
        size_t cmdPos = requestStr.find("CMD|");
        if (idPos != std::string::npos && cmdPos != std::string::npos) {
            size_t idEnd = requestStr.find("|", idPos + 7);
            reqId = std::stoul(requestStr.substr(idPos + 7, idEnd - (idPos + 7)));
            
            size_t cmdEnd = requestStr.find("|", cmdPos + 4);
            if (cmdEnd == std::string::npos) {
                cmd = requestStr.substr(cmdPos + 4);
            } else {
                cmd = requestStr.substr(cmdPos + 4, cmdEnd - (cmdPos + 4));
                size_t argPos = requestStr.find("ARG|");
                if (argPos != std::string::npos) {
                    arg = requestStr.substr(argPos + 4);
                }
            }
        } else {
            Log("SERVER", "Invalid packet structure received.");
            continue;
        }

        stats.total_requests++;
        Log("SERVER", "RECV requestId=" + std::to_string(reqId) + " cmd=" + cmd);

        std::string responsePayload = "";

        auto it = responseCache.find(reqId);
        if (it != responseCache.end()) {
            stats.duplicate_requests++;
            responsePayload = it->second;
            Log("SERVER", "DUPLICATE requestId=" + std::to_string(reqId) + " (served from cache)");
        } else {
            stats.unique_requests++;
            if (cmd == "TIME") {
                stats.time_cmds++;
                responsePayload = "STATUS:OK|RES:" + GetCurrentTimeString();
            } else if (cmd == "ECHO") {
                stats.echo_cmds++;
                responsePayload = "STATUS:OK|RES:" + arg;
            } else if (cmd == "ADD") {
                stats.add_cmds++;
                std::stringstream ss(arg);
                int a = 0, b = 0;
                if (ss >> a >> b) {
                    responsePayload = "STATUS:OK|RES:" + std::to_string(a + b);
                } else {
                    responsePayload = "STATUS:ERROR|RES:Invalid arguments for ADD";
                }
            } else if (cmd == "REVERSE") {
                stats.reverse_cmds++;
                std::string rev = arg;
                std::reverse(rev.begin(), rev.end());
                responsePayload = "STATUS:OK|RES:" + rev;
            } else if (cmd == "STATS") {
                stats.stats_cmds++;
                responsePayload = "STATUS:OK|RES:Total=" + std::to_string(stats.total_requests) +
                                  ", Unique=" + std::to_string(stats.unique_requests) +
                                  ", Duplicates=" + std::to_string(stats.duplicate_requests);
            } else {
                responsePayload = "STATUS:ERROR|RES:Unknown command";
            }

            if (responseCache.size() >= 100) {
                uint32_t oldestId = cacheKeys.front();
                cacheKeys.pop_front();
                responseCache.erase(oldestId);
            }
            responseCache[reqId] = responsePayload;
            cacheKeys.push_back(reqId);
        }

        if (drop_rate > 0.0 && dist(rng) < drop_rate) {
            Log("SERVER", "SIMULATED DROP response for requestId=" + std::to_string(reqId));
            continue;
        }

        std::string fullResponse = "REQ_ID|" + std::to_string(reqId) + "|" + responsePayload;
        sendto(serverSocket, fullResponse.c_str(), (int)fullResponse.length(), 0, (SOCKADDR*)&clientAddr, clientAddrLen);
        Log("SERVER", "SEND response for requestId=" + std::to_string(reqId));
    }

    closesocket(serverSocket);
    WSACleanup();
}

// Клиентская часть
void RunClient(const std::string& host, int port, int numRequests, int student_id) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(host.c_str());
    serverAddr.sin_port = htons(port);

    Log("CLIENT", "START target=" + host + ":" + std::to_string(port) + " requests=" + std::to_string(numRequests));

    std::mt19937 rng(1000 + student_id * 97);
    std::uniform_int_distribution<int> cmdDist(0, 4);
    std::uniform_int_distribution<int> numDist(1, 1000);

    std::vector<std::string> commands = {"TIME", "ECHO", "ADD", "REVERSE", "STATS"};

    int successCount = 0;
    int timeoutCount = 0;
    int totalRetries = 0;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 1; i <= numRequests; ++i) {
        uint32_t reqId = i;
        int cmdIndex = cmdDist(rng);
        std::string cmd = commands[cmdIndex];
        std::string arg = "";

        if (cmd == "ECHO") {
            arg = "TestMsg_" + std::to_string(i);
        } else if (cmd == "ADD") {
            arg = std::to_string(numDist(rng)) + " " + std::to_string(numDist(rng));
        } else if (cmd == "REVERSE") {
            arg = "String_" + std::to_string(i);
        }

        std::string requestStr = "REQ_ID|" + std::to_string(reqId) + "|CMD|" + cmd;
        if (!arg.empty()) {
            requestStr += "|ARG|" + arg;
        }

        bool received = false;
        int attempts = 0;
        const int maxAttempts = 3;

        while (attempts < maxAttempts && !received) {
            attempts++;
            if (attempts > 1) {
                totalRetries++;
                Log("CLIENT", "RETRY #" + std::to_string(attempts - 1) + " for requestId=" + std::to_string(reqId));
            }

            sendto(clientSocket, requestStr.c_str(), (int)requestStr.length(), 0, (SOCKADDR*)&serverAddr, sizeof(serverAddr));

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(clientSocket, &readSet);

            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000; // 200 мс

            int selResult = select(0, &readSet, NULL, NULL, &timeout);
            if (selResult == SOCKET_ERROR) {
                break;
            } else if (selResult == 0) {
                continue;
            } else {
                sockaddr_in fromAddr{};
                int fromLen = sizeof(fromAddr);
                char buffer[BUFFER_SIZE];
                int bytesRecv = recvfrom(clientSocket, buffer, BUFFER_SIZE - 1, 0, (SOCKADDR*)&fromAddr, &fromLen);
                if (bytesRecv != SOCKET_ERROR) {
                    buffer[bytesRecv] = '\0';
                    received = true;
                    successCount++;
                    if (i % 10 == 0 || i == 1) {
                        Log("CLIENT", "RECV response for requestId=" + std::to_string(reqId) + " (" + std::string(buffer) + ")");
                    }
                }
            }
        }

        if (!received) {
            timeoutCount++;
            Log("CLIENT", "TIMEOUT for requestId=" + std::to_string(reqId) + " after max retries");
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    std::cout << "\n================ STATISTIC SUMMARY ================\n";
    std::cout << "Total Requests Sent:   " << numRequests << "\n";
    std::cout << "Successful Responses:  " << successCount << "\n";
    std::cout << "Timeouts / Failures:   " << timeoutCount << "\n";
    std::cout << "Total Retries Made:    " << totalRetries << "\n";
    std::cout << "Total Execution Time:  " << std::fixed << std::setprecision(2) << totalMs << " ms\n";
    std::cout << "Average Time / Req:    " << (totalMs / numRequests) << " ms\n";
    std::cout << "===================================================\n";

    closesocket(clientSocket);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    std::string mode = "server";
    std::string host = "127.0.0.1";
    int port = DEFAULT_PORT;
    int requests = 100;
    double dropRate = 0.0;
    int student_id = 12;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--requests" && i + 1 < argc) requests = std::stoi(argv[++i]);
        else if (arg == "--drop-rate" && i + 1 < argc) dropRate = std::stod(argv[++i]); // Исправлено std::std::stod на std::stod
        else if (arg == "--id" && i + 1 < argc) student_id = std::stoi(argv[++i]);
    }

    if (mode == "server") {
        RunServer(port, dropRate, student_id);
    } else if (mode == "client") {
        RunClient(host, port, requests, student_id);
    } else {
        std::cout << "Unknown mode. Use --mode server or --mode client" << std::endl;
    }

    return 0;
}