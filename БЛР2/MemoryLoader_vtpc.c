// MemoryLoader_vtpc.c - ИСПРАВЛЕННАЯ ВЕРСИЯ
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <linux/fadvise.h>
#include <ctype.h>
#include <sys/types.h>
#include <errno.h>

#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150
#define BLOCK_SIZE 4096  // 4 KB

// Добавляем заголовок нашего кэша
#include "vtpc.h"

// Глобальные переменные для статистики кэша
extern int vtpc_cache_hits;
extern int vtpc_cache_misses;
extern int vtpc_cache_total;

// ========== ОПРЕДЕЛЕНИЯ СТРУКТУР ДО ИХ ИСПОЛЬЗОВАНИЯ ==========

// Вспомогательная функция для получения статистики CPU
typedef struct {
    double user;
    double system;
    double wait;
    unsigned long context_switches;
} cpu_stats_t;

// Структуры для мониторинга
typedef struct {
    double user_avg;
    double system_avg;
    double wait_avg;
    unsigned long context_switches_total;
    unsigned long context_switches_delta;
} monitoring_result_t;

typedef struct {
    volatile int monitoring;
    int max_processes;
} process_monitor_t;

typedef struct {
    off_t offset;     // Случайный индекс (смещение)
    int found_number; // Найдено ли число
} ReadData;

// Прототипы функций мониторинга
void* process_monitor_thread(void* arg);
void* cpu_monitoring(void* arg);
cpu_stats_t get_cpu_stats_from_proc(void);

// ========== ФУНКЦИИ МОНИТОРИНГА ==========

cpu_stats_t get_cpu_stats_from_proc() {
    cpu_stats_t stats = {-1.0, -1.0, -1.0, 0};
    
    FILE* stat = fopen("/proc/stat", "r");
    if (!stat) {
        return stats;
    }
    
    char buffer[256];
    
    if (fgets(buffer, sizeof(buffer), stat)) {
        unsigned long user, nice, system, idle, iowait, irq, softirq;
        if (sscanf(buffer, "cpu %lu %lu %lu %lu %lu %lu %lu", 
                  &user, &nice, &system, &idle, &iowait, &irq, &softirq) >= 4) {
            
            unsigned long total = user + nice + system + idle + iowait + irq + softirq;
            if (total > 0) {
                stats.user = (user + nice) * 100.0 / total;
                stats.system = system * 100.0 / total;
                stats.wait = iowait * 100.0 / total;
            }
        }
    }
    
    while (fgets(buffer, sizeof(buffer), stat)) {
        if (strncmp(buffer, "ctxt", 4) == 0) {
            unsigned long ctxt;
            if (sscanf(buffer, "ctxt %lu", &ctxt) == 1) {
                stats.context_switches = ctxt;
                break;
            }
        }
    }
    
    fclose(stat);
    return stats;
}

// Функция мониторинга CPU (ОДНА, без дубликатов)
void* cpu_monitoring(void* arg) {
    // Подавляем предупреждение о неиспользуемом параметре
    (void)arg;
    
    monitoring_result_t* result = malloc(sizeof(monitoring_result_t));
    if (!result) return NULL;

    double total_user = 0.0, total_wait = 0.0, total_sys = 0.0;
    int valid_samples = 0;
    unsigned long first_context_switches = 0;
    unsigned long last_context_switches = 0;
    
    for (int i = 0; i < 5; i++) {
        cpu_stats_t monitor = get_cpu_stats_from_proc();
        
        if (monitor.user >= 0 && monitor.system >= 0 && monitor.wait >= 0) {
            total_user += monitor.user;
            total_wait += monitor.wait;
            total_sys += monitor.system;
            valid_samples++;
            
            if (i == 0) {
                first_context_switches = monitor.context_switches;
            }
            last_context_switches = monitor.context_switches;
        }
        usleep(500000);
    }

    if (valid_samples > 0) {
        result->system_avg = total_sys / valid_samples;
        result->user_avg = total_user / valid_samples;
        result->wait_avg = total_wait / valid_samples;
        result->context_switches_total = last_context_switches;
        result->context_switches_delta = last_context_switches - first_context_switches;
    } else {
        result->system_avg = result->user_avg = result->wait_avg = -1.0;
        result->context_switches_total = result->context_switches_delta = 0;
    }

    return result;
}

// Функция мониторинга процессов
void* process_monitor_thread(void* arg) {
    process_monitor_t* monitor = (process_monitor_t*)arg;
    FILE *fp;
    char buffer[128];
    int loader_pid;
    int total_count = 0;
    int exclude_count = 0;

    fp = popen("ps -e -o pid= | wc -l", "r");
    if (!fp) {
        monitor->max_processes = 0;
        return NULL;
    }
    if (fscanf(fp, "%d", &total_count) != 1) {
        pclose(fp);
        monitor->max_processes = 0;
        return NULL;
    }
    pclose(fp);

    fp = popen("pgrep -f '^\\.\\/loaders$'", "r");
    if (!fp) {
        monitor->max_processes = total_count;
        return NULL;
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (sscanf(buffer, "%d", &loader_pid) == 1) {
            exclude_count++;
        }
    }
    pclose(fp);

    monitor->max_processes = exclude_count;
    return NULL;
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========

// Функция для создания выровненного буфера
void* allocate_aligned_buffer(size_t size, size_t alignment) {
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return NULL;
    }
    return ptr;
}

// ========== ФУНКЦИИ ТЕСТИРОВАНИЯ ==========

// Функция случайного чтения с использованием VTPC
ReadData randomReadTest_vtpc(int number) {
    ReadData result = {0};
    
    char number_str[MAX_NUM_LENGTH];
    snprintf(number_str, sizeof(number_str), "%d", number);
    srand((unsigned int)time(NULL) + getpid());
    
    // 1. ОТКРЫТИЕ ФАЙЛА
    int fd = vtpc_open("fragments_numbers_2.txt", O_RDONLY, 0);
    if (fd < 0) {
        return result;
    }
    
    // 2. ПОЛУЧЕНИЕ РАЗМЕРА
    off_t file_size = vtpc_lseek(fd, 0, SEEK_END);
    if (file_size <= BLOCK_SIZE) {
        vtpc_close(fd);
        return result;
    }
    vtpc_lseek(fd, 0, SEEK_SET);
    
    // 3. ВЫДЕЛЕНИЕ БУФЕРА
    char* buffer = malloc(BLOCK_SIZE);
    if (!buffer) {
        vtpc_close(fd);
        return result;
    }
    
    // 4. СЛУЧАЙНАЯ ПОЗИЦИЯ
    off_t max_offset = file_size - BLOCK_SIZE;
    if (max_offset <= 0) {
        free(buffer);
        vtpc_close(fd);
        return result;
    }
    
    off_t offset = (off_t)(rand() % (int)(max_offset / 4096)) * 4096;
    result.offset = offset;
    vtpc_lseek(fd, offset, SEEK_SET);
    
    // 5. ЧТЕНИЕ
    ssize_t bytesRead = vtpc_read(fd, buffer, BLOCK_SIZE);
    
    if (bytesRead != BLOCK_SIZE) {
        free(buffer);
        vtpc_close(fd);
        return result;
    }
    
    // 6. ПОИСК ЧИСЛА
    size_t num_len = strlen(number_str);
    
    for (size_t i = 0; i < BLOCK_SIZE - num_len; i++) {
        if (memcmp(buffer + i, number_str, num_len) == 0) {
            int left_ok = (i == 0) || !isalnum(buffer[i - 1]);
            int right_ok = (i + num_len >= BLOCK_SIZE) || !isalnum(buffer[i + num_len]);
            
            if (left_ok && right_ok) {
                memcpy(buffer + i, number_str, num_len);
                result.found_number = 1;
                break;
            }
        }
    }
    
    // 7. ЕСЛИ НАШЛИ - ЗАПИСЫВАЕМ ОБРАТНО
    if (result.found_number) {
        vtpc_lseek(fd, offset, SEEK_SET);
        vtpc_write(fd, buffer, BLOCK_SIZE);
        vtpc_fsync(fd);
    }
    
    // 8. НАГРУЗКА ПАМЯТИ - ВРЕМЕННЫЕ ФАЙЛЫ
    for (int j = 0; j < 10; j++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "temp_vtpc_%d_%d.tmp", getpid(), rand());
        
        int temp_fd = vtpc_open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (temp_fd >= 0) {
            for (int k = 0; k < 5; k++) {
                vtpc_lseek(temp_fd, 0, SEEK_SET);
                vtpc_write(temp_fd, buffer, BLOCK_SIZE);
                vtpc_fsync(temp_fd);
            }
            vtpc_close(temp_fd);
            unlink(filename);
        }
    }
    
    // 9. ОСВОБОЖДЕНИЕ РЕСУРСОВ
    free(buffer);
    vtpc_close(fd);
    
    return result;
}

// Функция случайного чтения с системными вызовами
ReadData randomReadTest_system(int number) {
    ReadData result = {0};

    char number_str[MAX_NUM_LENGTH];
    snprintf(number_str, sizeof(number_str), "%d", number);
    
    const size_t alignment = 512;
    const size_t aligned_block_size = ((BLOCK_SIZE + alignment - 1) / alignment) * alignment;
    
    #ifndef O_DIRECT
    #define O_DIRECT 040000
    #endif
    
    int fd = open("fragments_numbers_2.txt", O_RDONLY | O_DIRECT);
    if (fd == -1) {
        fd = open("fragments_numbers_2.txt", O_RDONLY);
        if (fd == -1) {
            return result;
        }
    }
    
    srand((unsigned int)time(NULL) + getpid());
    
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == -1) {
        close(fd);
        return result;
    }
    
    lseek(fd, 0, SEEK_SET);
    
    char* buffer = NULL;
    if (posix_memalign((void**)&buffer, alignment, aligned_block_size) != 0) {
        close(fd);
        return result;
    }
    
    if (file_size <= (off_t)aligned_block_size) {
        free(buffer);
        close(fd);
        return result;
    }

    off_t max_offset = file_size - aligned_block_size;
    off_t offset = (rand() % ((int)(max_offset / alignment))) * alignment;
    result.offset = offset;
    
    lseek(fd, offset, SEEK_SET);

    ssize_t bytesRead = read(fd, buffer, aligned_block_size);
    
    posix_fadvise(fd, offset, aligned_block_size, POSIX_FADV_DONTNEED);
    
    if (bytesRead != (ssize_t)aligned_block_size) {
        free(buffer);
        close(fd);
        return result;
    }

    size_t num_len = strlen(number_str);
    
    for (size_t i = 0; i < aligned_block_size - num_len; i++) {
        if (memcmp(buffer + i, number_str, num_len) == 0) {
            int left_ok = (i == 0) || !isalnum(buffer[i - 1]);
            int right_ok = (i + num_len >= aligned_block_size) || !isalnum(buffer[i + num_len]);
            
            if (left_ok && right_ok) {
                result.found_number = 1;
                break;
            }
        }
    }
    
    // НАГРУЗКА ПАМЯТИ - ВРЕМЕННЫЕ ФАЙЛЫ
    for (int j = 0; j < 10; j++) {
        char write_filename[256];
        sprintf(write_filename, "temp_io_load_system_%d_%d.tmp", getpid(), rand());
        
        int write_fd = open(write_filename, O_WRONLY | O_CREAT | O_DIRECT | O_SYNC, 0644);
        if (write_fd != -1) {
            for (int k = 0; k < 5; k++) {
                lseek(write_fd, 0, SEEK_SET);
                write(write_fd, buffer, bytesRead);
                fsync(write_fd);
            }
            close(write_fd);
            unlink(write_filename);
        }
    }
    
    free(buffer);
    close(fd);
    
    return result;
}

// ========== ОСНОВНЫЕ ФУНКЦИИ НАГРУЗКИ ==========

long long memory_loader_vtpc(int number, double* user_avg, double* system_avg, double* wait_avg,
                              unsigned long* context_switches_total, unsigned long* context_switches_delta, 
                              int* parallel_processes)
{
    *user_avg = 0.0;
    *system_avg = 0.0;
    *wait_avg = 0.0;
    *context_switches_total = 0;
    *context_switches_delta = 0;
    *parallel_processes = 0;

    pthread_t monitoring_thread_read;
    monitoring_result_t* result_read = NULL;

    process_monitor_t process_monitor = {
        .monitoring = 1,
        .max_processes = 0
    };
    pthread_t process_monitor_thread_id;
    
    struct timeval tv_start, tv_end;
    gettimeofday(&tv_start, NULL);
    
    int found_count = 0;
    int start_hits = vtpc_cache_hits;
    
    printf("\n--- Детали операций VTPC ---\n");
    printf("Итерация | Индекс (offset) | Попаданий в итерации | Всего попаданий\n");
    printf("---------|-----------------|----------------------|----------------\n");
    
    if (pthread_create(&monitoring_thread_read, NULL, cpu_monitoring, NULL) == 0) {
        pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor);
        
        for (int i = 0; i < 100; i++) {
            int hits_before = vtpc_cache_hits;
            ReadData result = randomReadTest_vtpc(number);
            int hits_this_iteration = vtpc_cache_hits - hits_before;
            
            if (result.found_number) {
                found_count++;
                printf("%9d | %15ld | %20d | %d\n", 
                       i+1, result.offset, hits_this_iteration, vtpc_cache_hits);
            } else {
                printf("%9d | %15ld | %20d | %d\n", 
                       i+1, result.offset, hits_this_iteration, vtpc_cache_hits);
            }
            
            if (found_count >= 3) break;
        }

        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread_id, NULL);
        *parallel_processes = process_monitor.max_processes;
        pthread_join(monitoring_thread_read, (void**)&result_read);
    } else {
        for (int i = 0; i < 100; i++) {
            int hits_before = vtpc_cache_hits;
            ReadData result = randomReadTest_vtpc(number);
            int hits_this_iteration = vtpc_cache_hits - hits_before;
            
            if (result.found_number) {
                found_count++;
                printf("%9d | %15ld |    ✓     | %20d | %d\n", 
                       i+1, result.offset, hits_this_iteration, vtpc_cache_hits);
            } else {
                printf("%9d | %15ld |    ✗     | %20d | %d\n", 
                       i+1, result.offset, hits_this_iteration, vtpc_cache_hits);
            }
            
            if (found_count >= 3) break;
        }
    }
    
    gettimeofday(&tv_end, NULL);

    if (result_read && result_read->user_avg >= 0) {
        *user_avg = result_read->user_avg;
        *system_avg = result_read->system_avg;
        *wait_avg = result_read->wait_avg;
        *context_switches_total = result_read->context_switches_total;
        *context_switches_delta = result_read->context_switches_delta;
        free(result_read);
    }

    printf("\nVTPC: Found number %d times\n", found_count);
    printf("Всего попаданий за тест: %d\n", vtpc_cache_hits - start_hits);
    
    return (tv_end.tv_sec - tv_start.tv_sec) * 1000000LL + 
           (tv_end.tv_usec - tv_start.tv_usec);
}

long long memory_loader_system(int number, double* user_avg, double* system_avg, double* wait_avg,
                                unsigned long* context_switches_total, unsigned long* context_switches_delta, 
                                int* parallel_processes)
{
    *user_avg = 0.0;
    *system_avg = 0.0;
    *wait_avg = 0.0;
    *context_switches_total = 0;
    *context_switches_delta = 0;
    *parallel_processes = 0;

    pthread_t monitoring_thread_read;
    monitoring_result_t* result_read = NULL;

    process_monitor_t process_monitor = {
        .monitoring = 1,
        .max_processes = 0
    };
    pthread_t process_monitor_thread_id;
    
    struct timeval tv_start, tv_end;
    gettimeofday(&tv_start, NULL);
    
    int found_count = 0;
    
    printf("\n--- Детали операций System ---\n");
    printf("Итерация | Индекс (offset) \n");
    printf("---------|-----------------\n");
    
    if (pthread_create(&monitoring_thread_read, NULL, cpu_monitoring, NULL) == 0) {
        pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor);
        
        for (int i = 0; i < 100; i++) {
            ReadData result = randomReadTest_system(number);
            
            if (result.found_number) {
                found_count++;
                printf("%9d | %15ld \n", i+1, result.offset);
            } else {
                printf("%9d | %15ld \n", i+1, result.offset);
            }
            
            if (found_count >= 3) break;
        }

        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread_id, NULL);
        *parallel_processes = process_monitor.max_processes;
        pthread_join(monitoring_thread_read, (void**)&result_read);
    } else {
        for (int i = 0; i < 100; i++) {
            ReadData result = randomReadTest_system(number);
            
            if (result.found_number) {
                found_count++;
                printf("%9d | %15ld \n", i+1, result.offset);
            } else {
                printf("%9d | %15ld \n", i+1, result.offset);
            }
            
            if (found_count >= 3) break;
        }
    }
    
    gettimeofday(&tv_end, NULL);

    if (result_read && result_read->user_avg >= 0) {
        *user_avg = result_read->user_avg;
        *system_avg = result_read->system_avg;
        *wait_avg = result_read->wait_avg;
        *context_switches_total = result_read->context_switches_total;
        *context_switches_delta = result_read->context_switches_delta;
        free(result_read);
    }

    printf("\nSystem: Found number %d times\n", found_count);
    
    return (tv_end.tv_sec - tv_start.tv_sec) * 1000000LL + 
           (tv_end.tv_usec - tv_start.tv_usec);
}

// ========== ФУНКЦИЯ СРАВНЕНИЯ ==========

void compare_performance() {
    printf("=== Сравнение производительности VTPC и системных вызовов ===\n\n");
    
    srand((unsigned int)time(NULL) + (unsigned int)getpid());
    
    FILE* test_file = fopen("fragments_numbers_2.txt", "r");
    if (!test_file) {
        printf("Ошибка: файл fragments_numbers_2.txt не найден!\n");
        printf("Создайте тестовый файл с помощью скрипта generate_fragments.py\n");
        return;
    }
    fclose(test_file);
    
    int test_number = 12345;
    
    vtpc_cache_hits = 0;
    vtpc_cache_misses = 0;
    vtpc_cache_total = 0;
    
    printf("Запуск теста с VTPC...\n");
    double vtpc_user, vtpc_system, vtpc_wait;
    unsigned long vtpc_ctx_total, vtpc_ctx_delta;
    int vtpc_processes;
    
    long long vtpc_time_us = memory_loader_vtpc(test_number, &vtpc_user, &vtpc_system, &vtpc_wait,
                                                &vtpc_ctx_total, &vtpc_ctx_delta, &vtpc_processes);
    
    printf("\n--- Итоговая статистика кэша VTPC ---\n");
    printf("Всего попаданий (hits): %d\n", vtpc_cache_hits);
    printf("Всего промахов (misses): %d\n", vtpc_cache_misses);
    if (vtpc_cache_hits + vtpc_cache_misses > 0) {
        printf("Процент попаданий: %.2f%%\n", 
               100.0 * vtpc_cache_hits / (vtpc_cache_hits + vtpc_cache_misses));
    }
    
    printf("\nЗапуск теста с системными вызовами...\n");
    double sys_user, sys_system, sys_wait;
    unsigned long sys_ctx_total, sys_ctx_delta;
    int sys_processes;
    
    long long sys_time_us = memory_loader_system(test_number, &sys_user, &sys_system, &sys_wait,
                                                 &sys_ctx_total, &sys_ctx_delta, &sys_processes);
    
    printf("\n=== РЕЗУЛЬТАТЫ СРАВНЕНИЯ ===\n");
    printf("VTPC:\n");
    printf("  Время выполнения: %lld мкс (%.3f сек)\n", vtpc_time_us, (double)vtpc_time_us / 1000000.0);
    printf("  Загрузка CPU: user=%.1f%%, system=%.1f%%, wait=%.1f%%\n", vtpc_user, vtpc_system, vtpc_wait);
    printf("  Переключения контекста: %lu\n", vtpc_ctx_delta);
    printf("  Процессов: %d\n", vtpc_processes);
    printf("  Cache Hits: %d\n", vtpc_cache_hits);
    printf("  Cache Misses: %d\n", vtpc_cache_misses);
    
    printf("\nSystem Calls:\n");
    printf("  Время выполнения: %lld мкс (%.3f сек)\n", sys_time_us, (double)sys_time_us / 1000000.0);
    printf("  Загрузка CPU: user=%.1f%%, system=%.1f%%, wait=%.1f%%\n", sys_user, sys_system, sys_wait);
    printf("  Переключения контекста: %lu\n", sys_ctx_delta);
    printf("  Процессов: %d\n", sys_processes);
    
    printf("\n=== СРАВНЕНИЕ ===\n");
    if (sys_time_us > 0 && vtpc_time_us > 0) {
        double speedup = (double)sys_time_us / vtpc_time_us;
        printf("Ускорение VTPC vs System: %.2fx\n", speedup);
        
        if (speedup > 1.0) {
            printf("✅ VTPC работает БЫСТРЕЕ системных вызовов\n");
        } else if (speedup < 1.0) {
            printf("⚠️ VTPC работает МЕДЛЕННЕЕ системных вызовов\n");
        } else {
            printf("⚖️ Производительность одинаковая\n");
        }
    }
}

// ========== ТОЧКА ВХОДА ==========

int main() {
    printf("=== Memory Loader с VTPC Cache ===\n");
    
    // Инициализация генератора случайных чисел
    srand((unsigned int)time(NULL) + (unsigned int)getpid());
    
    compare_performance();
    
    vtpc_cleanup();
    
    return 0;
}