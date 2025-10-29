#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include "CpuLoader.h"
#include "MemoryLoader.h"

typedef struct {
    int thread_id;
    int number;
    char* text;
    clock_t execution_time;
    double user_avg;
    double system_avg;
    double wait_avg;
    unsigned long context_switches_total;
    unsigned long context_switches_delta;
    int parallel_processes;
} thread_data_t;

void* cpu_loader_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    
    // ИНИЦИАЛИЗИРУЕМ локальные переменные перед вызовом
    double user_avg = 0.0;
    double system_avg = 0.0;
    double wait_avg = 0.0;
    unsigned long context_switches_total = 0;
    unsigned long context_switches_delta = 0;
    int parallel_processes = 0;
    
    data->execution_time = cpu_loader(data->text, &user_avg, &system_avg, &wait_avg, 
                                     &context_switches_total, &context_switches_delta,
                                     &parallel_processes);
    
    // Копируем результаты в структуру
    data->user_avg = user_avg;
    data->system_avg = system_avg;
    data->wait_avg = wait_avg;
    data->context_switches_total = context_switches_total;
    data->context_switches_delta = context_switches_delta;
    data->parallel_processes = parallel_processes;
    
    return NULL;
}

void* memory_loader_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    
    // ИНИЦИАЛИЗИРУЕМ локальные переменные перед вызовом
    double user_avg = 0.0;
    double system_avg = 0.0;
    double wait_avg = 0.0;
    unsigned long context_switches_total = 0;
    unsigned long context_switches_delta = 0;
    int parallel_processes = 0;
    
    data->execution_time = memory_loader(data->number, &user_avg, &system_avg, &wait_avg,
                                        &context_switches_total, &context_switches_delta,
                                        &parallel_processes);
    
    // Копируем результаты в структуру
    data->user_avg = user_avg;
    data->system_avg = system_avg;
    data->wait_avg = wait_avg;
    data->context_switches_total = context_switches_total;
    data->context_switches_delta = context_switches_delta;
    data->parallel_processes = parallel_processes;
    
    return NULL;
}

int main(int argc, char *argv[]) {
    // Парсинг аргументов командной строки
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <cpu_threads> <memory_threads> [number]\n", argv[0]);
        return 1;
    }
    
    int count_cpu_loader = atoi(argv[1]);
    int count_memory_loader = atoi(argv[2]);
    int number = 0;
    
    if (count_memory_loader > 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: number required for memory loader\n");
            return 1;
        }
        number = atoi(argv[3]);
    }

    char* cpu_text = NULL;
    
    if (count_cpu_loader > 0){
        cpu_text = create_text();
        if (cpu_text == NULL) {
            fprintf(stderr, "Error: failed to create text\n");
            return 1;
        }
    }
    
    // ИНИЦИАЛИЗИРУЕМ массивы структур
    pthread_t cpu_threads[count_cpu_loader];
    pthread_t memory_threads[count_memory_loader];
    thread_data_t cpu_data[count_cpu_loader];
    thread_data_t memory_data[count_memory_loader];
    
    // ЗАНУЛЯЕМ структуры данных
    memset(cpu_data, 0, sizeof(cpu_data));
    memset(memory_data, 0, sizeof(memory_data));
    
    clock_t total_start = clock();
    
    // Запуск потоков CPU
    for (int i = 0; i < count_cpu_loader; i++) {
        cpu_data[i].thread_id = i + 1;
        cpu_data[i].number = number;
        cpu_data[i].text = cpu_text;
        if (pthread_create(&cpu_threads[i], NULL, cpu_loader_thread, &cpu_data[i]) != 0) {
            fprintf(stderr, "Error: failed to create CPU thread %d\n", i);
            return 1;
        }
    }
    
    // Запуск потоков Memory
    if (count_memory_loader > 0) {
        for (int i = 0; i < count_memory_loader; i++) {
            memory_data[i].thread_id = i + 1;
            memory_data[i].number = number;
            if (pthread_create(&memory_threads[i], NULL, memory_loader_thread, &memory_data[i]) != 0) {
                fprintf(stderr, "Error: failed to create Memory thread %d\n", i);
                return 1;
            }
        }
    }
    
    // Ожидание завершения потоков
    for (int i = 0; i < count_cpu_loader; i++) {
        pthread_join(cpu_threads[i], NULL);
    }
    for (int i = 0; i < count_memory_loader; i++) {
        pthread_join(memory_threads[i], NULL);
    }
    
    clock_t total_end = clock();
    
    // Агрегация результатов (упрощенная)
    double total_user_avg = 0.0, total_system_avg = 0.0, total_wait_avg = 0.0;
    unsigned long total_context_switches_delta = 0;
    int total_process = 0;
    clock_t total_cpu_time = 0, total_memory_time = 0;
    
    for (int i = 0; i < count_cpu_loader; i++) {
        total_cpu_time += cpu_data[i].execution_time;
        total_user_avg += cpu_data[i].user_avg;
        total_system_avg += cpu_data[i].system_avg;
        total_wait_avg += cpu_data[i].wait_avg;
        total_context_switches_delta += cpu_data[i].context_switches_delta;
        total_process += cpu_data[i].parallel_processes;
        
    }
    
    for (int i = 0; i < count_memory_loader; i++) {
        total_memory_time += memory_data[i].execution_time;
        total_user_avg += memory_data[i].user_avg;
        total_system_avg += memory_data[i].system_avg;
        total_wait_avg += memory_data[i].wait_avg;
        total_context_switches_delta += memory_data[i].context_switches_delta;
        total_process += memory_data[i].parallel_processes;
    }

    // Вычисляем средние значения
    int total_threads = count_cpu_loader + count_memory_loader;
    if (total_threads > 0) {
        total_user_avg /= total_threads;
        total_system_avg /= total_threads;
        total_wait_avg /= total_threads;
        total_context_switches_delta /= total_threads;
        total_process /= total_threads;
    }
    
    // Вывод результатов
    printf("%ld,%.1f,%.1f,%.1f,%lu, %d\n", 
           (total_cpu_time + total_memory_time),
           total_user_avg, 
           total_system_avg, 
           total_wait_avg,
           total_context_switches_delta,
           total_process);
    
    // Освобождаем память
    if (cpu_text != NULL) {
        free(cpu_text);
    }
    
    return 0;
}