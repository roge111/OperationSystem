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

#include "CpuLoader.h"

// Чтение текста блоками мелкого размера (как в прошлом году)
// Найти блок с числом для замены, заменить его, и записать в файл тот же

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

//Стурктура возвращаемых данных из randomReadTest
typedef struct 
{
    clock_t time_total; // Время четния
    int found_number; // Найдено ли число
} ReadData;




#define BLOCK_SIZE (1024 * 1024)  // 1 МБ


/**
 * @brief Выполняет случайное чтение блока данных из файла и измеряет время операции
 *
 * Функция открывает файл, определяет его размер, генерирует случайное смещение
 * в пределах файла и читает блок данных размером BLOCK_SIZE по этому смещению.
 * Измеряется время выполнения операции чтения.
 *
 * @param filename Имя файла для чтения
 * @return double Время выполнения операции чтения в микросекундах, 0.0 в случае ошибки
 */
ReadData randomReadTest(int number) {
    // printf("Start\n");
    ReadData result = { .time_total = 0, .found_number = 0 };

    char number_str[MAX_NUM_LENGTH];
    snprintf(number_str, sizeof(number_str), "%d", number);
    
    // Для O_DIRECT требуется выравнивание буфера и размера блока
    // Обычно размер страницы 4KB, но для надежности используем 512 байт
    const size_t alignment = 512;
    const size_t aligned_block_size = ((BLOCK_SIZE + alignment - 1) / alignment) * alignment;
    
    // Определяем O_DIRECT если он не определен
    #ifndef O_DIRECT
    #define O_DIRECT 040000
    #endif
    
    // Открываем файл с флагом O_DIRECT для отключения кэширования
    int fd = open("fragments_numbers_2.txt", O_RDONLY | O_DIRECT);
    if (fd == -1) {
        // Пытаемся открыть без O_DIRECT если предыдущая попытка не удалась
        fd = open("fragments_numbers_2.txt", O_RDONLY);
        if (fd == -1) {
            printf("Error: failed to open file\n");
            return result;
        }
    }
    if (fd == -1) {
        printf("Error: failed to open file with O_DIRECT\n");
        // Пытаемся открыть без O_DIRECT если предыдущая попытка не удалась
        fd = open("fragments_numbers_2.txt", O_RDONLY);
        if (fd == -1) {
            printf("Error: failed to open file\n");
            return result;
        }
    }
    
    // Получаем размер файла
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == -1) {
        printf("Error: failed to get file size\n");
        close(fd);
        return result;
    }
    
    // Возвращаемся в начало файла
    if (lseek(fd, 0, SEEK_SET) == -1) {
        printf("Error: failed to seek file\n");
        close(fd);
        return result;
    }
    
    // Выделяем выровненный буфер
    char* buffer = NULL;
    if (posix_memalign((void**)&buffer, alignment, aligned_block_size) != 0) {
        printf("Error: failed to allocate aligned memory\n");
        close(fd);
        return result;
    }
    
    // Проверяем размер файла, что он не меньше размера блока
    if (file_size <= (off_t)aligned_block_size) {
        printf("Error: file size is too small\n");
        free(buffer);
        close(fd);
        return result;
    }

    // Генерация случайного смещения (в пределах [0, file_size - aligned_block_size])
    srand((unsigned int)time(NULL));
    // Для O_DIRECT смещение также должно быть выровнено
    off_t offset = (rand() % ((file_size - (off_t)aligned_block_size) / alignment)) * alignment;
    // printf("Offset: %ld\n", (long)offset);

    // Перемещаемся на случайную позицию
    if (lseek(fd, offset, SEEK_SET) == -1) {
        printf("Error: failed to seek file\n");
        free(buffer);
        close(fd);
        return result;
    }

    // Замер времени
    clock_t start_read = clock();
    
    // Чтение блока данных
    ssize_t bytesRead = read(fd, buffer, aligned_block_size);
    // printf("Bytes read: %zd, BLOCK_SIZE: %zu\n", bytesRead, aligned_block_size);

    // Отключаем кэширование после чтения
    posix_fadvise(fd, offset, aligned_block_size, POSIX_FADV_DONTNEED);

    clock_t end_read = clock();
    
    // Добавляем небольшую задержку, чтобы убедиться, что время измеряется корректно
    usleep(1);

    // Проверка успешности чтения
    if (bytesRead != (ssize_t)aligned_block_size) {
        printf("Error: failed to read file, read %zd bytes\n", bytesRead);
        free(buffer);
        close(fd);
        return result;
    }

    // Поиск числа (как строки) в блоке
    size_t num_len = strlen(number_str);
    if (num_len == 0 || num_len > 150) {
        free(buffer);
        close(fd);
        return result;  // некорректная длина числа
    }
    const char* data = buffer;
    size_t remaining = aligned_block_size;
    const char* match = NULL;

    clock_t start_replace = 0;
    clock_t end_replace = 0;
    int replaced = 0; // Флаг, показывающий, была ли выполнена замена
    while ((match = memmem(data, remaining, number_str, num_len)) != NULL) {
        size_t pos = match - buffer;

        // Проверка левой границы: начало буфера или не цифра/буква
        int left_ok = (pos == 0) || !isalnum(buffer[pos - 1]);

        // Проверка правой границы: конец буфера или не цифра/буква
        int right_ok = (pos + num_len >= aligned_block_size) || !isalnum(buffer[pos + num_len]);

        if (left_ok && right_ok) {
            start_replace = clock();
            // Заменяем найденное число в буфере
            memcpy(buffer + pos, number_str, num_len);
            end_replace = clock();
            result.found_number = 1;
            replaced = 1; // Устанавливаем флаг замены
            break;
        }

        data = match + 1;
        remaining = buffer + aligned_block_size - data;
    }
    clock_t start_write = clock();
    
    // Записываем измененный блок в другой файл, если число было найдено
    if (result.found_number == 1) {
        FILE* output_file = fopen("modified_block.txt", "w");
        if (output_file != NULL) {
            fwrite(buffer, 1, aligned_block_size, output_file);
            fclose(output_file);
        }
    }
    clock_t end_write = clock();
    // printf("Time total: %ld, %ld, %ld, %ld, %ld, %ld\n", start_write, end_write, start_replace, end_replace, start_read, end_read);
    // Вычисляем общее время выполнения
    result.time_total = (end_read - start_read); // Время чтения всегда учитывается
    if (replaced) {
        result.time_total += (end_replace - start_replace); // Время замены, если она была
    }
    if (result.found_number == 1) {
        result.time_total += (end_write - start_write); // Время записи, если число было найдено
    }
    
    // Освобождаем ресурсы
    free(buffer);
    close(fd);


    // ДОБАВЛЯЕМ ИНТЕНСИВНУЮ ЗАПИСЬ
    clock_t start_io_write = clock();
    
    // Создаем 10 временных файлов и пишем в них
    for (int j = 0; j < 10; j++) {
        char write_filename[256];
        sprintf(write_filename, "temp_io_load_%d_%d.tmp", getpid(), rand());
        
        int write_fd = open(write_filename, O_WRONLY | O_CREAT | O_DIRECT | O_SYNC, 0644);
        if (write_fd != -1) {
            // Пишем несколько раз в тот же файл
            for (int k = 0; k < 5; k++) {
                lseek(write_fd, 0, SEEK_SET);
                write(write_fd, buffer, bytesRead);
                fsync(write_fd);  // Принудительная синхронизация
            }
            close(write_fd);
            unlink(write_filename);  // Удаляем сразу
        }
    }
    
    clock_t end_io_write = clock();

    result.time_total = end_io_write - start_io_write;
    
    return result;
}




void use_data(char* str) {
    volatile char dummy;  // volatile — чтобы компилятор не оптимизировал
    while (*str) {
        dummy = *str;  // «используем» каждый символ
        (void)dummy;   
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

    // char array[ARRAY_SIZE][MAX_NUM_LENGTH];
    // int count = 0;

    //const int TARGET_LINES = 1000000;      // Сколько случайных строк читать
    //const long TOTAL_LINES = 10000000L; // Всего строк в файле

   


    

    
    // Мониторинг во время чтения
    pthread_t monitoring_thread_read;
    monitoring_result_t* result_read = NULL;

    process_monitor_t process_monitor = {
        .monitoring = 1,
        .max_processes = 0
    };
    pthread_t process_monitor_thread_id;

    
    clock_t total_time_read = 0;
    if (pthread_create(&monitoring_thread_read, NULL, cpu_monitoring, NULL) == 0) {
        pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor);
        // Далее  код чтения блоков
        
        for (int i = 0; i < 100; i++){
            ReadData result= randomReadTest(number);
            //printf("%ld\n", (long)result.time_total);
            total_time_read += result.time_total;
            // Останавдливаем, если мы нашли в блоке число.
            // if (result.found_number == 1){
            //     break;
            // }

            if (i == 100) break;
        }

        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread_id, NULL);
        *parallel_processes = process_monitor.max_processes;

        pthread_join(monitoring_thread_read, (void**)&result_read);
    } else {
        // Без мониторинга: читаем случайные строки
        
    }
    //for (int index = 0; index < ARRAY_SIZE; index++){use_data(array[index]);}

   
    // Закрываем файл

    // ОБРАБОТКА ДАННЫХ И ЗАПИСЬ
    // pthread_t monitoring_thread_write;
    // monitoring_result_t* result_write = NULL;

    // process_monitor.monitoring = 1;
    // process_monitor.max_processes = 0;
    // clock_t time_work = 0;

    // if (pthread_create(&monitoring_thread_write, NULL, cpu_monitoring, NULL) == 0) {
    //     pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor);
    //     time_work = memory_work(number, array, count);
    //     process_monitor.monitoring = 0;
    //     pthread_join(process_monitor_thread_id, NULL);
    //     *parallel_processes = (*parallel_processes + process_monitor.max_processes) / 2;
    //     pthread_join(monitoring_thread_write, (void**)&result_write);
    // } else {
    //     time_work = memory_work(number, array, count);
    // }

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

    // if (result_write && result_write->user_avg >= 0) {
    //     *user_avg += result_write->user_avg;
    //     *system_avg += result_write->system_avg;
    //     *wait_avg += result_write->wait_avg;
    //     *context_switches_total += result_write->context_switches_total;
    //     *context_switches_delta += result_write->context_switches_delta;
    //     valid_samples++;
    //     free(result_write);
    // }

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
    
    return total_time_read;
}



