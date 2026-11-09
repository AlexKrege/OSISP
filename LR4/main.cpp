#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <iostream>
#include <vector>
#include <iomanip>
#include <cstdlib>

#define ARRAY_SIZE 16
#define NUM_READERS 3
#define NUM_WRITERS 2
#define OPS_PER_PARTICIPANT 200

// Структура защищаемой записи
struct SharedRecord {
    int A;
    int B;
    int C;
    unsigned long long version;
    SRWLOCK lock;
};

// Контекст для передачи параметров в потоки
struct ThreadContext {
    int thread_id;
    bool is_writer;
    bool use_global_lock;
    bool access_mode_hotspot; // false - равномерный, true - хотспот (первые 2 записи)
    int ops_count;
    
    SharedRecord* records;
    SRWLOCK* global_lock;
    
    long* invariant_violations;
    LONG* active_threads;
};

// Выбор индекса записи с учетом режима доступа
int SelectRecordIndex(bool hotspot) {
    if (!hotspot) {
        // Равномерный доступ по всем 16 записям
        return rand() % ARRAY_SIZE;
    } else {
        // Хотспот: 80% запросов идут на записи 0 и 1
        int r = rand() % 100;
        if (r < 80) {
            return rand() % 2; 
        } else {
            return 2 + (rand() % (ARRAY_SIZE - 2));
        }
    }
}

// Функция потока-писателя
DWORD WINAPI WriterThreadProc(LPVOID lpParam) {
    ThreadContext* ctx = (ThreadContext*)lpParam;
    
    for (int i = 0; i < ctx->ops_count; i++) {
        int index = SelectRecordIndex(ctx->access_mode_hotspot);
        
        // Захват блокировки
        if (ctx->use_global_lock) {
            AcquireSRWLockExclusive(ctx->global_lock);
        } else {
            AcquireSRWLockExclusive(&ctx->records[index].lock);
        }
        
        // Критическая секция записи
        SharedRecord* rec = &ctx->records[index];
        rec->version++;
        rec->A = (rand() % 100) + 1;
        rec->B = rec->A * 2;
        rec->C = rec->A * 3;
        
        // Имитация длительности обработки операции
        Sleep(1);
        
        // Освобождение блокировки
        if (ctx->use_global_lock) {
            ReleaseSRWLockExclusive(ctx->global_lock);
        } else {
            ReleaseSRWLockExclusive(&ctx->records[index].lock);
        }
    }
    
    InterlockedDecrement(ctx->active_threads);
    return 0;
}

// Функция потока-читателя
DWORD WINAPI ReaderThreadProc(LPVOID lpParam) {
    ThreadContext* ctx = (ThreadContext*)lpParam;
    
    for (int i = 0; i < ctx->ops_count; i++) {
        int index = SelectRecordIndex(ctx->access_mode_hotspot);
        
        // Захват блокировки в разделяемом режиме (Shared)
        if (ctx->use_global_lock) {
            AcquireSRWLockShared(ctx->global_lock);
        } else {
            AcquireSRWLockShared(&ctx->records[index].lock);
        }
        
        // Критическая секция чтения
        SharedRecord* rec = &ctx->records[index];
        int a = rec->A;
        int b = rec->B;
        int c = rec->C;
        
        // Проверка инвариантов: B = 2*A, C = 3*A
        if (a != 0 && (b != a * 2 || c != a * 3)) {
            InterlockedIncrement(ctx->invariant_violations);
        }
        
        // Освобождение блокировки
        if (ctx->use_global_lock) {
            ReleaseSRWLockShared(ctx->global_lock);
        } else {
            ReleaseSRWLockShared(&ctx->records[index].lock);
        }
    }
    
    InterlockedDecrement(ctx->active_threads);
    return 0;
}

// Запуск одного экспериментального теста
double RunExperiment(bool use_global_lock, bool hotspot, long& violations_out) {
    SharedRecord records[ARRAY_SIZE];
    SRWLOCK global_lock;
    
    InitializeSRWLock(&global_lock);
    for (int i = 0; i < ARRAY_SIZE; i++) {
        InitializeSRWLock(&records[i].lock);
        records[i].A = 1;
        records[i].B = 2;
        records[i].C = 3;
        records[i].version = 0;
    }
    
    long violations = 0;
    LONG active_threads = NUM_READERS + NUM_WRITERS;
    
    std::vector<HANDLE> threads;
    std::vector<ThreadContext> contexts(NUM_READERS + NUM_WRITERS);
    
    // Создание потоков читателей
    for (int i = 0; i < NUM_READERS; i++) {
        contexts[i] = {
            i + 1, false, use_global_lock, hotspot, 
            OPS_PER_PARTICIPANT, records, &global_lock, 
            &violations, &active_threads
        };
        HANDLE hThread = CreateThread(NULL, 0, ReaderThreadProc, &contexts[i], 0, NULL);
        threads.push_back(hThread);
    }
    
    // Создание потоков писателей
    for (int i = 0; i < NUM_WRITERS; i++) {
        int idx = NUM_READERS + i;
        contexts[idx] = {
            i + 1, true, use_global_lock, hotspot, 
            OPS_PER_PARTICIPANT, records, &global_lock, 
            &violations, &active_threads
        };
        HANDLE hThread = CreateThread(NULL, 0, WriterThreadProc, &contexts[idx], 0, NULL);
        threads.push_back(hThread);
    }
    
    // Высокоточное измерение времени (QueryPerformanceCounter)
    LARGE_INTEGER frequency, start_time, end_time;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start_time);
    
    // Ожидание завершения всех потоков
    WaitForMultipleObjects(threads.size(), threads.data(), TRUE, INFINITE);
    
    QueryPerformanceCounter(&end_time);
    
    // Закрытие дескрипторов потоков
    for (HANDLE h : threads) {
        CloseHandle(h);
    }
    
    violations_out = violations;
    double elapsed_ms = (double)(end_time.QuadPart - start_time.QuadPart) * 1000.0 / frequency.QuadPart;
    return elapsed_ms;
}

int main() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    
    std::cout << "========================================================\n";
    std::cout << " ЛАБОРАТОРНАЯ РАБОТА: Взаимодействие процессов и потоков \n";
    std::cout << " Вариант 12: Блокировки записей (SRW Lock)               \n";
    std::cout << "========================================================\n\n";
    
    struct ConfigInfo {
        std::string name;
        bool use_global;
        bool hotspot;
    };
    
    ConfigInfo configs[4] = {
        { "Конфигурация А: Глобальный SRW Lock + Равномерный доступ", true, false },
        { "Конфигурация Б: Глобальный SRW Lock + Хотспот (на 2 записи)", true, true },
        { "Конфигурация В: Тонкие (индивидуальные) SRW Locks + Равномерный", false, false },
        { "Конфигурация Г: Тонкие (индивидуальные) SRW Locks + Хотспот", false, true }
    };
    
    std::cout << std::left << std::setw(65) << "Конфигурация" 
              << std::setw(12) << "__Запуск 1__" 
              << std::setw(12) << "__Запуск 2__"
              << std::setw(12) << "__Запуск 3__"
              << std::setw(15) << "__Среднее, мс__"
              << std::setw(15) << "__Нарушения__" << "\n";
    std::cout << std::string(130, '-') << "\n";
    
    for (int c = 0; c < 4; c++) {
        double runs[3];
        long total_violations = 0;
        
        for (int run = 0; run < 3; run++) {
            long violations = 0;
            runs[run] = RunExperiment(configs[c].use_global, configs[c].hotspot, violations);
            total_violations += violations;
        }
        
        double avg_time = (runs[0] + runs[1] + runs[2]) / 3.0;
        
        std::cout << std::left << std::setw(65) << configs[c].name
                  << std::setw(12) << std::fixed << std::setprecision(2) << runs[0]
                  << std::setw(12) << runs[1]
                  << std::setw(12) << runs[2]
                  << std::setw(15) << avg_time
                  << std::setw(15) << total_violations << "\n";
    }
    
    std::cout << std::string(130, '-') << "\n";
    std::cout << "\nЭксперимент завершен. Все инварианты проверены.\n";
    
    return 0;
}