#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>

#include "CpuLoader.h"


#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150



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



void use_data(char* str) {
    volatile char dummy;  // volatile — чтобы компилятор не оптимизировал
    while (*str) {
        dummy = *str;  // «используем» каждый символ
        str++;
    }
}

/**
 * @brief Выполняет поиск и замену числа в массиве строк, затем записывает результат в файл
 *
 * Функция преобразует входное число в строку и ищет его в массиве строк.
 * При нахождении заменяет элемент массива на строковое представление числа.
 * Результат записывается в файл output.txt.
 *
 * @param number Целое число для поиска и замены
 * @param array Двумерный массив строк, в котором производится поиск
 * @param count Количество элементов в массиве для обработки
 * 
 * Вовзращает время замены числа + время записи в файл
 */
clock_t memory_work(int number, char array[ARRAY_SIZE][MAX_NUM_LENGTH], int count) {
    char number_str[MAX_NUM_LENGTH];
    sprintf(number_str, "%d", number);
    
    // Защита от выхода за границы
    if (count > ARRAY_SIZE) {
        count = ARRAY_SIZE;
    }
    clock_t start_replace = 0;
    clock_t end_replace = 0;
    // Ищем и заменяем число
    for (int i = 0; i < count; i++) {
        if (atoi(array[i]) == number) {
            start_replace = clock();
            strcpy(array[i], number_str);
            end_replace = clock();
        
            break;
        }
    }

    clock_t time_replace = end_replace - start_replace;
    
    // Записываем в файл
    clock_t start_write = clock();
    FILE* ptr = fopen("output.txt", "w");
    
    if (ptr != NULL) {
        for (int i = 0; i < count; i++) {
            fprintf(ptr, "%s\n", array[i]);
        }
        fclose(ptr);
    }
    clock_t end_write = clock();
    clock_t time_write = end_write - start_write;
    
    return time_replace + time_write;
}

/**
 * @brief Загружает данные из файла с отключенным кэшированием и мониторит использование CPU
 *
 * Функция читает данные из файла fragments_numbers.txt, отключая кэширование,
 * выполняет обработку данных и записывает результат в файл output.txt.
 * Во время выполнения операций мониторится использование CPU и количество процессов.
 *
 * @param number Целое число для поиска и замены в данных
 * @param user_avg Указатель для возврата среднего значения времени в пользовательском режиме CPU
 * @param system_avg Указатель для возврата среднего значения времени в системном режиме CPU
 * @param wait_avg Указатель для возврата среднего значения времени ожидания CPU
 * @param context_switches_total Указатель для возврата общего количества переключений контекста
 * @param context_switches_delta Указатель для возврата изменения количества переключений контекста
 * @param parallel_processes Указатель для возврата максимального количества параллельных процессов
 * @return clock_t Общее время выполнения операций чтения и записи
 */

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

    const int TARGET_LINES = 1000000;      // Сколько случайных строк читать
    const long TOTAL_LINES = 10000000L; // Всего строк в файле

    // ЧТЕНИЕ СЛУЧАЙНЫХ СТРОК ИЗ ФАЙЛА
    #ifndef O_DIRECT
    #define O_DIRECT 00040000 /* direct disk access hint */
    #endif

    int fd = open("fragments_numbers.txt", O_RDONLY | O_DIRECT);
    if (fd == -1) {
        fd = open("fragments_numbers.txt", O_RDONLY);
        if (fd == -1) {
            *user_avg = *system_avg = *wait_avg = -1.0;
            return 1;
        }
    }

    FILE* ptr = fdopen(fd, "r");
    if (ptr == NULL) {
        close(fd);
        *user_avg = *system_avg = *wait_avg = -1.0;
        return 1;
    }

    setbuf(ptr, NULL);
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED | POSIX_FADV_SEQUENTIAL);

    // Генерируем 10 000 случайных номеров строк (1..10 000 000)
    srand(time(NULL) ^ (unsigned int)pthread_self());
    int* random_line_nums = calloc(TARGET_LINES, sizeof(int));
    if (!random_line_nums) {
        fclose(ptr);
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
    pthread_t process_monitor_thread_id;

    clock_t start_read = clock();

    if (pthread_create(&monitoring_thread_read, NULL, cpu_monitoring, NULL) == 0) {
        pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor);

        for (int i = 0; i < TARGET_LINES; i++) {
            random_line_nums[i] = rand() % TOTAL_LINES + 1;
        }

        // Читаем файл построчно, ищем нужные строки
        char line_buf[MAX_NUM_LENGTH + 10];
        long line_counter = 0;

        while (fgets(line_buf, sizeof(line_buf), ptr) != NULL) {
            line_counter++;

            for (int i = 0; i < TARGET_LINES; i++) {
                if (line_counter == random_line_nums[i]) {
                    if (count < ARRAY_SIZE) {
                        strncpy(array[count], line_buf, MAX_NUM_LENGTH);
                        array[count][strcspn(array[count], "\n")] = '\0'; // Убираем \n
                        count++;
                    }
                    break;
                }
            }
        }

        free(random_line_nums);

        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread_id, NULL);
        *parallel_processes = process_monitor.max_processes;

        pthread_join(monitoring_thread_read, (void**)&result_read);
    } else {
        // Без мониторинга: читаем случайные строки
        srand(time(NULL) ^ (unsigned int)pthread_self());
        char line_buf[MAX_NUM_LENGTH + 10];

        for (int i = 0; i < TARGET_LINES; i++) {
            int target_line = rand() % TOTAL_LINES + 1;
            long line_counter = 0;
            rewind(ptr);

            while (fgets(line_buf, sizeof(line_buf), ptr) != NULL) {
                line_counter++;
                if (line_counter == target_line) {
                    if (count < ARRAY_SIZE) {
                        strncpy(array[count], line_buf, MAX_NUM_LENGTH);
                        array[count][strcspn(array[count], "\n")] = '\0';
                        count++;
                    }
                    break;
                }
            }
        }
    }
    for (int index = 0; index < ARRAY_SIZE; index++){use_data(array[index]);}

    clock_t end_read = clock();
    fclose(ptr); // Закрываем файл

    // ОБРАБОТКА ДАННЫХ И ЗАПИСЬ
    pthread_t monitoring_thread_write;
    monitoring_result_t* result_write = NULL;

    process_monitor.monitoring = 1;
    process_monitor.max_processes = 0;
    clock_t time_work = 0;

    if (pthread_create(&monitoring_thread_write, NULL, cpu_monitoring, NULL) == 0) {
        pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor);
        time_work = memory_work(number, array, count);
        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread_id, NULL);
        *parallel_processes = (*parallel_processes + process_monitor.max_processes) / 2;
        pthread_join(monitoring_thread_write, (void**)&result_write);
    } else {
        time_work = memory_work(number, array, count);
    }

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

    if (valid_samples > 0) {
        *user_avg /= valid_samples;
        *system_avg /= valid_samples;
        *wait_avg /= valid_samples;
    } else {
        *user_avg = *system_avg = *wait_avg = -1.0;
        *context_switches_total = 0;
        *context_switches_delta = 0;
    }

    // Возвращаем общее время: чтение + обработка + запись
    clock_t time_read = end_read - start_read;
    return time_read + time_work;
}
