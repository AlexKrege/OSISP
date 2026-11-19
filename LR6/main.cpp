//if (!(Test-Path "c:\Users\e8\Desktop\OSISP\LR6\output")) { New-Item -ItemType Directory -Path "c:\Users\e8\Desktop\OSISP\LR6\output" | Out-Null }
// g++ -Wall -Wextra -g3 "c:\Users\e8\Desktop\OSISP\LR6\main.cpp" -o "c:\Users\e8\Desktop\OSISP\LR6\output\main.exe"
//& "c:\Users\e8\Desktop\OSISP\LR6\output\main.exe" --student 059 --run test10 --seed-count 10 --seed 1097 --yes
//& "c:\Users\e8\Desktop\OSISP\LR6\output\main.exe" --student 059 --run test10 --rollback

#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

template <typename T, size_t N>
constexpr size_t ArrayCount(const T (&)[N]) {
    return N;
}

struct RegistryEntrySnapshot {
    std::wstring subKeyPath; // Относительно HKEY_CURRENT_USER
    std::wstring valueName;
    DWORD valueType = REG_NONE;
    std::vector<BYTE> data;
};

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()),
                        &out[0], needed, nullptr, nullptr);
    return out;
}

static std::wstring LocalToWide(const char* str) {
    if (!str || !*str) return std::wstring();
    int needed = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
    if (needed <= 1) return std::wstring();
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, str, -1, &out[0], needed);
    return out;
}

static std::wstring GetNowTimestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%02u:%02u:%02u.%03u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::wstring(buf);
}

static void LogEvent(std::wofstream& logFile,
                     const std::wstring& op,
                     const std::wstring& obj,
                     const std::wstring& status,
                     LONG errCode = 0) {
    std::wostringstream oss;
    oss << L"[" << GetNowTimestamp() << L"] " << op;
    if (!obj.empty()) oss << L" object=" << obj;
    oss << L" status=" << status;
    if (errCode != 0) oss << L" code=" << errCode;

    std::wcout << oss.str() << L"\n";
    if (logFile.is_open()) {
        logFile << oss.str() << L"\n";
        logFile.flush();
    }
}

static void PrintWin32Error(const wchar_t* funcName, const wchar_t* operation, LONG statusCode, std::wofstream& logFile) {
    LPWSTR msgBuf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, static_cast<DWORD>(statusCode),
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPWSTR>(&msgBuf), 0, nullptr);
    std::wstring msg = (len && msgBuf) ? std::wstring(msgBuf) : std::wstring(L"Unknown error");
    while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) {
        msg.pop_back();
    }

    std::wcerr << L"[ERROR] op=" << operation
               << L" func=" << funcName
               << L" code=" << statusCode
               << L" msg=\"" << msg << L"\"\n";

    std::wostringstream oss;
    oss << L"[" << GetNowTimestamp() << L"] " << operation
        << L" func=" << funcName
        << L" status=ERR code=" << statusCode
        << L" msg=\"" << msg << L"\"";
    if (logFile.is_open()) {
        logFile << oss.str() << L"\n";
        logFile.flush();
    }
    if (msgBuf) LocalFree(msgBuf);
}

static const wchar_t* RegTypeToString(DWORD type) {
    switch (type) {
        case REG_SZ: return L"REG_SZ";
        case REG_EXPAND_SZ: return L"REG_EXPAND_SZ";
        case REG_DWORD: return L"REG_DWORD";
        case REG_QWORD: return L"REG_QWORD";
        case REG_MULTI_SZ: return L"REG_MULTI_SZ";
        case REG_BINARY: return L"REG_BINARY";
        default: return L"REG_OTHER";
    }
}

static std::wstring BuildSandboxPath(const std::wstring& studentId, const std::wstring& runId) {
    return L"Software\\OSiSP\\Lab06\\Student_" + studentId + L"\\Run_" + runId;
}

static bool IsAllowedSandboxPath(const std::wstring& relPath, const std::wstring& studentId, const std::wstring& runId) {
    std::wstring prefix = BuildSandboxPath(studentId, runId);
    if (relPath.size() < prefix.size()) return false;
    return _wcsnicmp(relPath.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static bool SeedTestObjects(const std::wstring& sandboxPath,
                            const std::wstring& studentId,
                            const std::wstring& runId,
                            size_t count,
                            unsigned int seed,
                            std::wofstream& logFile) {
    if (!IsAllowedSandboxPath(sandboxPath, studentId, runId)) {
        std::wcerr << L"[BLOCKED] Path outside sandbox: " << sandboxPath << L"\n";
        return false;
    }

    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, sandboxPath.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) {
        PrintWin32Error(L"RegCreateKeyExW", L"REG_CREATE_SEED", rc, logFile);
        return false;
    }

    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> typeDist(0, 2);
    std::uniform_int_distribution<DWORD> dwordDist(1, 1000000);
    std::uniform_int_distribution<int> byteDist(0, 255);

    for (size_t i = 0; i < count; ++i) {
        wchar_t nameBuf[64];
        swprintf_s(nameBuf, L"TestItem_%03zu", i + 1);
        int kind = typeDist(rng);

        if (kind == 0) {
            std::wstring val = L"SampleValue_" + std::to_wstring(seed) + L"_" + std::to_wstring(i + 1);
            const BYTE* pData = reinterpret_cast<const BYTE*>(val.c_str());
            DWORD cbData = static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t));
            rc = RegSetValueExW(hKey, nameBuf, 0, REG_SZ, pData, cbData);
        } else if (kind == 1) {
            DWORD dv = dwordDist(rng);
            rc = RegSetValueExW(hKey, nameBuf, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dv), sizeof(DWORD));
        } else {
            std::vector<BYTE> bin(16);
            for (size_t b = 0; b < bin.size(); ++b) {
                bin[b] = static_cast<BYTE>(byteDist(rng));
            }
            rc = RegSetValueExW(hKey, nameBuf, 0, REG_BINARY, &bin[0], static_cast<DWORD>(bin.size()));
        }

        if (rc != ERROR_SUCCESS) {
            PrintWin32Error(L"RegSetValueExW", L"REG_SET_SEED", rc, logFile);
            RegCloseKey(hKey);
            return false;
        }
    }

    RegCloseKey(hKey);
    LogEvent(logFile, L"REG_SEED", sandboxPath + L" (count=" + std::to_wstring(count) + L")", L"OK");
    return true;
}

static bool CollectValues(HKEY hRoot,
                          const std::wstring& relPath,
                          std::vector<RegistryEntrySnapshot>& outEntries,
                          std::wofstream& logFile) {
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(hRoot, relPath.c_str(), 0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS) {
        PrintWin32Error(L"RegOpenKeyExW", L"REG_OPEN_READ", rc, logFile);
        return false;
    }

    DWORD index = 0;
    wchar_t nameBuf[512];
    while (true) {
        DWORD nameLen = static_cast<DWORD>(ArrayCount(nameBuf));
        DWORD type = 0;
        DWORD dataSize = 0;
        rc = RegEnumValueW(hKey, index, nameBuf, &nameLen, nullptr, &type, nullptr, &dataSize);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) {
            PrintWin32Error(L"RegEnumValueW", L"REG_ENUM_VALUE", rc, logFile);
            RegCloseKey(hKey);
            return false;
        }

        RegistryEntrySnapshot entry;
        entry.subKeyPath = relPath;
        entry.valueName.assign(nameBuf, nameLen);
        entry.valueType = type;
        entry.data.resize(dataSize);

        DWORD actualSize = dataSize;
        rc = RegQueryValueExW(hKey, entry.valueName.c_str(), nullptr, &entry.valueType,
                              dataSize > 0 ? &entry.data[0] : nullptr, &actualSize);
        if (rc != ERROR_SUCCESS) {
            PrintWin32Error(L"RegQueryValueExW", L"REG_QUERY_VALUE", rc, logFile);
            RegCloseKey(hKey);
            return false;
        }
        entry.data.resize(actualSize);
        outEntries.push_back(entry);
        ++index;
    }

    RegCloseKey(hKey);
    LogEvent(logFile, L"REG_SCAN", relPath + L" count=" + std::to_wstring(outEntries.size()), L"OK");
    return true;
}

static bool SaveSnapshotBinary(const std::wstring& filePath, const std::vector<RegistryEntrySnapshot>& entries) {
    std::string utf8Path = WideToUtf8(filePath);
    std::ofstream out(utf8Path.c_str(), std::ios::binary);
    if (!out.is_open()) return false;

    uint32_t count = static_cast<uint32_t>(entries.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (size_t i = 0; i < entries.size(); ++i) {
        const RegistryEntrySnapshot& e = entries[i];
        uint32_t pathLen = static_cast<uint32_t>(e.subKeyPath.size() * sizeof(wchar_t));
        uint32_t nameLen = static_cast<uint32_t>(e.valueName.size() * sizeof(wchar_t));
        uint32_t dataLen = static_cast<uint32_t>(e.data.size());

        out.write(reinterpret_cast<const char*>(&pathLen), sizeof(pathLen));
        if (pathLen > 0) out.write(reinterpret_cast<const char*>(e.subKeyPath.data()), pathLen);

        out.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) out.write(reinterpret_cast<const char*>(e.valueName.data()), nameLen);

        out.write(reinterpret_cast<const char*>(&e.valueType), sizeof(e.valueType));

        out.write(reinterpret_cast<const char*>(&dataLen), sizeof(dataLen));
        if (dataLen > 0) {
            out.write(reinterpret_cast<const char*>(&e.data[0]), dataLen);
        }
    }
    return out.good();
}

static bool LoadSnapshotBinary(const std::wstring& filePath, std::vector<RegistryEntrySnapshot>& outEntries) {
    outEntries.clear();
    std::string utf8Path = WideToUtf8(filePath);
    std::ifstream in(utf8Path.c_str(), std::ios::binary);
    if (!in.is_open()) return false;

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in.good()) return false;

    for (uint32_t i = 0; i < count; ++i) {
        RegistryEntrySnapshot e;
        uint32_t pathLen = 0, nameLen = 0, dataLen = 0;

        in.read(reinterpret_cast<char*>(&pathLen), sizeof(pathLen));
        e.subKeyPath.resize(pathLen / sizeof(wchar_t));
        if (pathLen > 0) in.read(reinterpret_cast<char*>(&e.subKeyPath[0]), pathLen);

        in.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
        e.valueName.resize(nameLen / sizeof(wchar_t));
        if (nameLen > 0) in.read(reinterpret_cast<char*>(&e.valueName[0]), nameLen);

        in.read(reinterpret_cast<char*>(&e.valueType), sizeof(e.valueType));

        in.read(reinterpret_cast<char*>(&dataLen), sizeof(dataLen));
        e.data.resize(dataLen);
        if (dataLen > 0) in.read(reinterpret_cast<char*>(&e.data[0]), dataLen);

        if (!in.good()) return false;
        outEntries.push_back(e);
    }
    return true;
}

static bool DeletePlanEntries(const std::vector<RegistryEntrySnapshot>& entries,
                              const std::wstring& studentId,
                              const std::wstring& runId,
                              size_t failAfterCount,
                              size_t& deletedCountOut,
                              double& elapsedMsOut,
                              std::wofstream& logFile) {
    deletedCountOut = 0;
    elapsedMsOut = 0.0;

    LARGE_INTEGER freq{}, tStart{}, tEnd{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&tStart);

    HKEY hKey = nullptr;
    LONG rc = ERROR_SUCCESS;
    std::wstring currentOpenedPath;

    for (size_t i = 0; i < entries.size(); ++i) {
        if (!IsAllowedSandboxPath(entries[i].subKeyPath, studentId, runId)) {
            std::wcerr << L"[BLOCKED] Attempt outside allowed sandbox: " << entries[i].subKeyPath << L"\n";
            LogEvent(logFile, L"REG_DELETE", entries[i].valueName, L"BLOCKED", ERROR_ACCESS_DENIED);
            if (hKey) RegCloseKey(hKey);
            return false;
        }

        if (failAfterCount > 0 && deletedCountOut >= failAfterCount) {
            std::wcerr << L"[SIMULATED_FAULT] Forced error after deleting " << deletedCountOut << L" objects.\n";
            LogEvent(logFile, L"REG_DELETE", entries[i].valueName, L"SIMULATED_FAIL", ERROR_GEN_FAILURE);
            if (hKey) RegCloseKey(hKey);
            QueryPerformanceCounter(&tEnd);
            elapsedMsOut = static_cast<double>(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
            return false;
        }

        if (hKey == nullptr || currentOpenedPath != entries[i].subKeyPath) {
            if (hKey) {
                RegCloseKey(hKey);
                hKey = nullptr;
            }
            rc = RegOpenKeyExW(HKEY_CURRENT_USER, entries[i].subKeyPath.c_str(), 0, KEY_SET_VALUE, &hKey);
            if (rc != ERROR_SUCCESS) {
                PrintWin32Error(L"RegOpenKeyExW", L"REG_OPEN_WRITE", rc, logFile);
                return false;
            }
            currentOpenedPath = entries[i].subKeyPath;
        }

        rc = RegDeleteValueW(hKey, entries[i].valueName.c_str());
        if (rc != ERROR_SUCCESS) {
            PrintWin32Error(L"RegDeleteValueW", L"REG_DELETE_VALUE", rc, logFile);
            RegCloseKey(hKey);
            return false;
        }
        ++deletedCountOut;
    }

    if (hKey) RegCloseKey(hKey);
    QueryPerformanceCounter(&tEnd);
    elapsedMsOut = static_cast<double>(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    LogEvent(logFile, L"REG_DELETE_BATCH", L"deleted=" + std::to_wstring(deletedCountOut), L"OK");
    return true;
}

static bool RestoreEntries(const std::vector<RegistryEntrySnapshot>& entries,
                           const std::wstring& studentId,
                           const std::wstring& runId,
                           size_t& restoredCountOut,
                           double& elapsedMsOut,
                           std::wofstream& logFile) {
    restoredCountOut = 0;
    elapsedMsOut = 0.0;

    LARGE_INTEGER freq{}, tStart{}, tEnd{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&tStart);

    for (size_t i = 0; i < entries.size(); ++i) {
        const RegistryEntrySnapshot& entry = entries[i];
        if (!IsAllowedSandboxPath(entry.subKeyPath, studentId, runId)) {
            std::wcerr << L"[BLOCKED] Restore outside sandbox: " << entry.subKeyPath << L"\n";
            LogEvent(logFile, L"REG_RESTORE", entry.valueName, L"BLOCKED", ERROR_ACCESS_DENIED);
            return false;
        }
        HKEY hKey = nullptr;
        LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, entry.subKeyPath.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr);
        if (rc != ERROR_SUCCESS) {
            PrintWin32Error(L"RegCreateKeyExW", L"REG_CREATE_RESTORE", rc, logFile);
            return false;
        }

        const BYTE* ptr = entry.data.empty() ? nullptr : &entry.data[0];
        DWORD cbData = static_cast<DWORD>(entry.data.size());
        rc = RegSetValueExW(hKey, entry.valueName.c_str(), 0, entry.valueType, ptr, cbData);
        RegCloseKey(hKey);

        if (rc != ERROR_SUCCESS) {
            PrintWin32Error(L"RegSetValueExW", L"REG_SET_RESTORE", rc, logFile);
            return false;
        }
        ++restoredCountOut;
    }

    QueryPerformanceCounter(&tEnd);
    elapsedMsOut = static_cast<double>(tEnd.QuadPart - tStart.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    LogEvent(logFile, L"REG_RESTORE_BATCH", L"restored=" + std::to_wstring(restoredCountOut), L"OK");
    return true;
}

int main(int argc, char* argv[]) {
    std::wstring studentId = L"001";
    std::wstring runId = L"run1";
    size_t seedCount = 0;
    unsigned int seed = 1097;
    size_t failAfter = 0;
    bool rollbackMode = false;
    bool autoConfirm = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--student") == 0 && i + 1 < argc) {
            studentId = LocalToWide(argv[++i]);
        } else if (strcmp(argv[i], "--run") == 0 && i + 1 < argc) {
            runId = LocalToWide(argv[++i]);
        } else if (strcmp(argv[i], "--seed-count") == 0 && i + 1 < argc) {
            seedCount = static_cast<size_t>(strtoull(argv[++i], nullptr, 10));
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = static_cast<unsigned int>(strtoul(argv[++i], nullptr, 10));
        } else if (strcmp(argv[i], "--fail-after") == 0 && i + 1 < argc) {
            failAfter = static_cast<size_t>(strtoull(argv[++i], nullptr, 10));
        } else if (strcmp(argv[i], "--rollback") == 0) {
            rollbackMode = true;
        } else if (strcmp(argv[i], "--yes") == 0) {
            autoConfirm = true;
        }
    }

    std::wstring snapshotFile = L"snapshot_" + studentId + L"_" + runId + L".bin";
    std::wstring logFilePath = L"lab06_var12_" + studentId + L"_" + runId + L".log";
    std::string utf8LogPath = WideToUtf8(logFilePath);
    std::wofstream logFile(utf8LogPath.c_str(), std::ios::app);

    std::wstring sandboxPath = BuildSandboxPath(studentId, runId);

    if (rollbackMode) {
        std::vector<RegistryEntrySnapshot> snap;
        if (!LoadSnapshotBinary(snapshotFile, snap)) {
            std::wcerr << L"Failed to load snapshot: " << snapshotFile << L"\n";
            return 1;
        }
        size_t restored = 0;
        double ms = 0.0;
        if (!RestoreEntries(snap, studentId, runId, restored, ms, logFile)) {
            return 1;
        }
        std::wcout << std::fixed << std::setprecision(3)
                   << L"Rollback complete: restored=" << restored
                   << L" time_ms=" << ms << L"\n";
        return 0;
    }

    if (seedCount > 0) {
        if (!SeedTestObjects(sandboxPath, studentId, runId, seedCount, seed, logFile)) {
            return 1;
        }
    }

    std::vector<RegistryEntrySnapshot> entries;
    if (!CollectValues(HKEY_CURRENT_USER, sandboxPath, entries, logFile)) {
        return 1;
    }

    std::wcout << L"\n=== PLAN FOR DELETION (" << entries.size() << L" objects) ===\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        std::wcout << L"  [" << (i + 1) << L"] " << entries[i].valueName
                   << L" | type=" << RegTypeToString(entries[i].valueType)
                   << L" | bytes=" << entries[i].data.size() << L"\n";
    }

    if (!SaveSnapshotBinary(snapshotFile, entries)) {
        std::wcerr << L"Failed to save snapshot before deletion.\n";
        return 1;
    }
    LogEvent(logFile, L"SNAPSHOT_SAVE", snapshotFile, L"OK");

    if (!autoConfirm) {
        std::wcout << L"Proceed with deletion? (y/n): ";
        wchar_t ans = L'n';
        std::wcin >> ans;
        if (ans != L'y' && ans != L'Y') {
            LogEvent(logFile, L"REG_DELETE_PLAN", L"cancelled_by_user", L"CANCELLED");
            return 0;
        }
    }

    size_t deleted = 0;
    double elapsedMs = 0.0;
    bool ok = DeletePlanEntries(entries, studentId, runId, failAfter, deleted, elapsedMs, logFile);
    std::wcout << std::fixed << std::setprecision(3)
               << L"Result: ok=" << (ok ? L"true" : L"false")
               << L" deleted=" << deleted << L"/" << entries.size()
               << L" time_ms=" << elapsedMs << L"\n";
    return ok ? 0 : 2;
}