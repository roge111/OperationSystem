#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include "CpuLoader.h"

#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150

typedef struct {
    double user;
    double system;
    double wait;
    unsigned long context_switches;
} cpu_stats_t;

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



void* simple_process_monitor(void* arg) {
    process_monitor_t* monitor = (process_monitor_t*)arg;
    
    int check_count = 0;
    while (monitor->monitoring && check_count < 2) {
        FILE *fp = popen("ps aux | grep -c \"[.]/loaders\"", "r");
        if (fp) {
            int count = 0;
            if (fscanf(fp, "%d", &count) == 1) {
                if (count > 1) {
                    int parallel_count = count - 2;
                    if (parallel_count > 0 && parallel_count > monitor->max_processes) {
                        monitor->max_processes = parallel_count;
                    }
                }
            }
            pclose(fp);
        }
        check_count++;
        sleep(1);
    }
    
    return NULL;
}

void memory_work(int number, char array[ARRAY_SIZE][MAX_NUM_LENGTH], int count) {
    char number_str[MAX_NUM_LENGTH];
    sprintf(number_str, "%d", number);
    
    // Защита от выхода за границы
    if (count > ARRAY_SIZE) {
        count = ARRAY_SIZE;
    }
    
    // Ищем и заменяем число
    for (int i = 0; i < count; i++) {
        if (atoi(array[i]) == number) {
            strcpy(array[i], number_str);
            break;
        }
    }
    
    // Записываем в файл
    FILE* ptr = fopen("output.txt", "w");
    if (ptr != NULL) {
        for (int i = 0; i < count; i++) {
            fprintf(ptr, "%s\n", array[i]);
        }
        fclose(ptr);
    }
}

clock_t memory_loader(int number, double* user_avg, double* system_avg, double* wait_avg, 
                     unsigned long* context_switches_total, unsigned long* context_switches_delta, int* parallel_processes)
{
    // Инициализация выходных параметров
    *user_avg = 0.0;
    *system_avg = 0.0;
    *wait_avg = 0.0;
    *context_switches_total = 0;
    *context_switches_delta = 0;
    *parallel_processes = 0;
    
    char array[ARRAY_SIZE][MAX_NUM_LENGTH];
    int count = 0;
    
    // ЧТЕНИЕ ФАЙЛА
    FILE* ptr = fopen("fragments_numbers.txt", "r");
    if (ptr == NULL) {
        *user_avg = *system_avg = *wait_avg = -1.0;
        return 1;
    }

    // Мониторинг во время чтения
    pthread_t monitoring_thread_read;
    monitoring_result_t* result_read = NULL;
    
    process_monitor_t process_monitor = {
        .monitoring = 1,
        .max_processes = 0
    };
    pthread_t process_monitor_thread;
    
    clock_t start_read = clock();
    
    // Запускаем мониторинг CPU для чтения
    if (pthread_create(&monitoring_thread_read, NULL, cpu_monitoring, NULL) == 0) {
        // Запускаем простой мониторинг процессов
        pthread_create(&process_monitor_thread, NULL, simple_process_monitor, &process_monitor);
        
        // Читаем файл с защитой от переполнения
        while (count < ARRAY_SIZE && fscanf(ptr, "%149s", array[count]) == 1) { 
            count++;
        }
        
        // Останавливаем мониторинг процессов
        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread, NULL);
        *parallel_processes = process_monitor.max_processes;
        
        // Получаем результаты мониторинга CPU
        pthread_join(monitoring_thread_read, (void**)&result_read);
    } else {
        // Если не удалось запустить мониторинг, просто читаем файл
        while (count < ARRAY_SIZE && fscanf(ptr, "%149s", array[count]) == 1) { 
            count++;
        }
    }
    
    clock_t end_read = clock();
    fclose(ptr);
    
    // ОБРАБОТКА ДАННЫХ И ЗАПИСЬ
    clock_t start_write = clock();
    
    // Мониторинг во время записи
    pthread_t monitoring_thread_write;
    monitoring_result_t* result_write = NULL;
    
    // Сбрасываем мониторинг процессов для записи
    process_monitor.monitoring = 1;
    process_monitor.max_processes = 0;
    
    if (pthread_create(&monitoring_thread_write, NULL, cpu_monitoring, NULL) == 0) {
        pthread_create(&process_monitor_thread, NULL, simple_process_monitor, &process_monitor);
        
        // Выполняем основную работу
        memory_work(number, array, count);
        
        // Останавливаем мониторинг процессов
        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread, NULL);
        *parallel_processes = (*parallel_processes + process_monitor.max_processes) / 2;
        
        // Получаем результаты мониторинга CPU для записи
        pthread_join(monitoring_thread_write, (void**)&result_write);
    } else {
        // Если не удалось запустить мониторинг, просто выполняем работу
        memory_work(number, array, count);
    }
    
    clock_t end_write = clock();
    
    // ОБРАБОТКА РЕЗУЛЬТАТОВ МОНИТОРИНГА
    int valid_samples = 0;
    
    if (result_read && result_read->user_avg >= 0) {
        *user_avg += result_read->user_avg;
        *system_avg += result_read->system_avg;
        *wait_avg += result_read->wait_avg;
        *context_switches_total += result_read->context_switches_total;
        *context_switches_delta += result_read->context_switches_delta;
        valid_samples++;
        free(result_read);
    }
    
    if (result_write && result_write->user_avg >= 0) {
        *user_avg += result_write->user_avg;
        *system_avg += result_write->system_avg;
        *wait_avg += result_write->wait_avg;
        *context_switches_total += result_write->context_switches_total;
        *context_switches_delta += result_write->context_switches_delta;
        valid_samples++;
        free(result_write);
    }
    
    // Вычисляем средние значения
    if (valid_samples > 0) {
        *user_avg /= valid_samples;
        *system_avg /= valid_samples;
        *wait_avg /= valid_samples;
    } else {
        *user_avg = *system_avg = *wait_avg = -1.0;
        *context_switches_total = *context_switches_delta = 0;
    }
    
    // Возвращаем общее время
    clock_t time_read = end_read - start_read;
    clock_t time_write = end_write - start_write;
    return time_read + time_write;
}