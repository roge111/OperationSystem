#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include "CpuLoader.h"
#include "MemoryLoader.h"

// Структура для передачи данных в потоки
typedef struct {
    int thread_id;
    int number;  // Для memory_loader
    char* text;  // ← ДОБАВЛЕНО: для передачи текста в cpu_loader
    clock_t execution_time;

    double user_avg;      // ← ДОБАВЛЕНО для статистики CPU
    double system_avg;    // ← ДОБАВЛЕНО для статистики CPU
    double wait_avg; 
} thread_data_t;

// Обновленная функция для потоков CPU
void* cpu_loader_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    // printf("Поток CPU %d начал работу\n", data->thread_id);
    
    // Вызываем cpu_loader с новой сигнатурой
    double user_avg, system_avg, wait_avg;
    data->execution_time = cpu_loader(data->text, &user_avg, &system_avg, &wait_avg);
    
    // Сохраняем статистику CPU
    data->user_avg = user_avg;
    data->system_avg = system_avg;
    data->wait_avg = wait_avg;
    
    // printf("Поток CPU %d завершил работу за %ld мс\n", data->thread_id, data->execution_time);
    return NULL;
}
// Функция для потоков Memory
void* memory_loader_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    // printf("Поток Memory %d начал работу с числом %d\n", data->thread_id, data->number);
    
    data->execution_time = memory_loader(data->number);
    
    // printf("Поток Memory %d завершил работу за %ld мс\n", data->thread_id, data->execution_time);
    return NULL;
}

int main(int argc, char *argv[]) {
    // Парсинг аргументов командной строки
    if (argc < 3) {
        // printf("Использование: %s <cpu_threads> <memory_threads> [memory_number]\n", argv[0]);
        return 1;
    }
    
    int count_cpu_loader = atoi(argv[1]);
    int count_memory_loader = atoi(argv[2]);
    int number = 0;
    
    if (count_memory_loader > 0) {
        if (argc < 4) {
            // printf("Ошибка: для memory нагрузчика требуется число\n");
            return 1;
        }
        number = atoi(argv[3]);
    }

    char* cpu_text = NULL;
    
    if (count_cpu_loader > 0){
        cpu_text = create_text();  // ← Теперь просто присваиваем
        if (cpu_text == NULL) {
            // printf("Ошибка: не удалось создать текст для CPU нагрузчика\n");
            return 1;
        }
    }
    
    // printf("Параметры: CPU потоков=%d, Memory потоков=%d, число=%d\n", count_cpu_loader, count_memory_loader, number);
    
    // Создание массивов для потоков
    pthread_t cpu_threads[count_cpu_loader];
    pthread_t memory_threads[count_memory_loader];
    thread_data_t cpu_data[count_cpu_loader];
    thread_data_t memory_data[count_memory_loader];
    
    // ← ДОБАВЛЕНО: подготовка текста для CPU нагрузчика
    //char* cpu_text = "test_string_for_cpu_loader";  // или любой другой текст
    
    clock_t total_start = clock();
    
    // Запуск потоков CPU
    // printf("Запуск %d потоков CPU...\n", count_cpu_loader);
    for (int i = 0; i < count_cpu_loader; i++) {
        cpu_data[i].thread_id = i + 1;
        cpu_data[i].number = number;
        cpu_data[i].text = cpu_text;  // ← ДОБАВЛЕНО: передаем текст
        if (pthread_create(&cpu_threads[i], NULL, cpu_loader_thread, &cpu_data[i]) != 0) {
            // printf("Ошибка создания потока CPU %d\n", i + 1);
            return 1;
        }
    }
    
    // Запуск потоков Memory
    if (count_memory_loader > 0) {
        // printf("Запуск %d потоков Memory...\n", count_memory_loader);
        for (int i = 0; i < count_memory_loader; i++) {
            memory_data[i].thread_id = i + 1;
            memory_data[i].number = number;
            if (pthread_create(&memory_threads[i], NULL, memory_loader_thread, &memory_data[i]) != 0) {
                // printf("Ошибка создания потока Memory %d\n", i + 1);
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
    double total_user_avg = 0.0, total_system_avg = 0.0, total_wait_avg = 0.0;
    
    // Вывод результатов
    // printf("\n=== РЕЗУЛЬТАТЫ ===\n");
    
    clock_t total_cpu_time = 0;
    for (int i = 0; i < count_cpu_loader; i++) {
        // printf("Поток CPU %d: %ld мс\n", cpu_data[i].thread_id, cpu_data[i].execution_time);
        
        total_cpu_time += cpu_data[i].execution_time;
        total_user_avg += cpu_data[i].user_avg;
        total_system_avg += cpu_data[i].system_avg;
        total_wait_avg += cpu_data[i].wait_avg;
    }
    
    clock_t total_memory_time = 0;
    for (int i = 0; i < count_memory_loader; i++) {
        // printf("Поток Memory %d: %ld мс\n", memory_data[i].thread_id, memory_data[i].execution_time);
        total_memory_time += memory_data[i].execution_time;
    }
    
    // printf("\nСуммарное время CPU: %ld мс\n", total_cpu_time);
    // printf("Суммарное время Memory: %ld мс\n", total_memory_time);
    printf("%ld,%ld,%.1f,%.1f,%.1f", (total_cpu_time + total_memory_time), total_cpu_time, total_user_avg, total_system_avg, total_wait_avg);
    
    return 0;
}