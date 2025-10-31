#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150
#define MAX_TEXT_LENGTH 500000

/**
 * @brief Структура для хранения статистики CPU
 *
 * Содержит информацию о загрузке процессора и количестве переключений контекста
 */
typedef struct
{
    double user;              ///< Время выполнения пользовательских процессов в процентах
    double system;            ///< Время выполнения системных процессов в процентах
    double wait;              ///< Время ожидания ввода-вывода в процентах
    unsigned long context_switches; ///< Общее количество переключений контекста
} cpu_stats_t;

/**
 * @brief Структура для хранения результатов мониторинга CPU
 *
 * Содержит усредненные значения загрузки процессора и изменение переключений контекста
 */
typedef struct
{
    double user_avg;              ///< Среднее значение загрузки пользовательских процессов в процентах
    double system_avg;            ///< Среднее значение загрузки системных процессов в процентах
    double wait_avg;              ///< Среднее значение времени ожидания ввода-вывода в процентах
    //unsigned long context_switches_total;
    unsigned long context_switches_delta; ///< Изменение количества переключений контекста за период мониторинга
} monitoring_result_t;

/**
 * @brief Структура для мониторинга процессов
 *
 * Содержит флаг активности мониторинга и максимальное количество процессов
 */
typedef struct {
    volatile int monitoring;  ///< Флаг активности мониторинга (1 - активен, 0 - остановлен)
    int max_processes;        ///< Максимальное количество параллельных процессов, зафиксированное во время мониторинга
} process_monitor_t;

/**
 * @brief Упрощенная функция мониторинга процессов, выполняющаяся в отдельном потоке
 *
 * Функция отслеживает количество параллельных процессов loaders и обновляет
 * максимальное значение в структуре process_monitor_t
 *
 * @param arg Указатель на структуру process_monitor_t с параметрами мониторинга
 * @return void* NULL в любом случае
 */
void* process_monitor_thread(void* arg) {
    process_monitor_t* monitor = (process_monitor_t*)arg;
    FILE *fp;
    char buffer[128];
    int loader_pid;
    int total_count = 0;
    int exclude_count = 0;

    // 1. Считаем ВСЕ процессы (упрощённо: процессы пользователя)
    fp = popen("ps -e -o pid= | wc -l", "r");
    if (!fp) {
        monitor->max_processes = 0;  // Фиксируем результат в структуре
        return NULL;  // Всегда возвращаем NULL
    }
    if (fscanf(fp, "%d", &total_count) != 1) {
        pclose(fp);
        monitor->max_processes = 0;
        return NULL;
    }
    pclose(fp);

    // 2. Находим PID процессов './loaders'
    fp = popen("pgrep -f '^\\.\\/loaders$'", "r");
    if (!fp) {
        monitor->max_processes = total_count;  // Если pgrep недоступен — считаем всё
        return NULL;
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (sscanf(buffer, "%d", &loader_pid) == 1) {
            exclude_count++;
        }
    }
    pclose(fp);

    // Записываем результат в структуру
    monitor->max_processes = total_count - exclude_count;
    return NULL;  // Всегда возвращаемся с NULL
}


/**
 * @brief Функция мониторинга CPU, получает статистику из /proc/stat
 *
 * Читает информацию о загрузке процессора из файла /proc/stat и вычисляет
 * процентные значения загрузки для различных типов процессов
 *
 * @return cpu_stats_t Структура с данными о загрузке CPU, в случае ошибки возвращает значения -1.0
 */
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

/**
 * @brief Функция мониторинга CPU, выполняющаяся в отдельном потоке
 *
 * Выполняет серию измерений загрузки CPU с интервалом и вычисляет средние значения.
 * Результаты возвращаются через выделенную структуру monitoring_result_t
 *
 * @param arg Не используется (зарезервировано для будущего использования)
 * @return void* Указатель на структуру monitoring_result_t с результатами мониторинга
 */
void* cpu_monitoring(void* arg) {
    monitoring_result_t* result = malloc(sizeof(monitoring_result_t));
    if (!result) return NULL;

    double total_user = 0.0, total_wait = 0.0, total_sys = 0.0;
    int valid_samples = 0;
    unsigned long first_context_switches = 0;
    unsigned long last_context_switches = 0;
    
    for (int i = 0; i < 5; i++) { // Уменьшили до 5 замеров
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
        usleep(500000); // 0.5 секунды вместо 1
    }

    if (valid_samples > 0) {
        result->system_avg = total_sys / valid_samples;
        result->user_avg = total_user / valid_samples;
        result->wait_avg = total_wait / valid_samples;
        //result->context_switches_total = last_context_switches;
        result->context_switches_delta = last_context_switches - first_context_switches;
    } else {
        result->system_avg = result->user_avg = result->wait_avg = -1.0;
        //result->context_switches_total = result->context_switches_delta = 0;
    }

    return result;
}

/**
 * @brief Упрощенная функция MD5 с проверкой границ
 *
 * Выполняет многократное вычисление MD5 хеша для заданного текста.
 * Используется для создания нагрузки на процессор.
 *
 * @param text Входной текст для хеширования
 * @param hash Выходной буфер для хеша (минимум 33 байта)
 */
void inefficient_md5(const char* text, char* hash) {
    MD5_CTX context;
    unsigned char digest[MD5_DIGEST_LENGTH];
    char large_buffer[500000];
    
    // ОЧЕНЬ многократное вычисление MD5
    for (int iteration = 0; iteration < 1000; iteration++) {
        // Многократное копирование данных
        for (int copy_iter = 0; copy_iter < 10; copy_iter++) {
            strcpy(large_buffer, text);
        }
        
        MD5_Init(&context);
        MD5_Update(&context, large_buffer, strlen(large_buffer));
        MD5_Final(digest, &context);
    }
    
    // Преобразование в hex
    char *output = hash;
    for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(output, "%02x", digest[i]);
        output += 2;
    }
    *output = '\0';
}

/**
 * @brief Создает текст из фрагментов, прочитанных из файла
 *
 * Читает фрагменты текста из файла "fragments_text.txt" и составляет из них
 * большую строку, выбирая фрагменты в случайном порядке
 *
 * @return char* Указатель на динамически выделенную строку с текстом,
 *         NULL в случае ошибки или если файл пуст
 */
char* create_text() {
    int array_size = ARRAY_SIZE;
    char array[ARRAY_SIZE][MAX_NUM_LENGTH];
    int count = 0;
    
    FILE* ptr = fopen("fragments_text.txt", "r");
    if (ptr == NULL) {
        // printf("Ошибка: файл fragments_text.txt не найден!\n");
        return NULL;  
    }
    
    while (count < array_size && fscanf(ptr, "%149s", array[count]) == 1) {
        count++;
    }
    fclose(ptr);
    
    if (count == 0) {
        // printf("Файл пуст!\n");
        return NULL;  
    }
    
    int min = 0;
    int max = count - 1;
    srand(time(NULL));

    // ВЫДЕЛЯЕМ ПАМЯТЬ ДИНАМИЧЕСКИ вместо локального массива
    char* text = (char*)malloc(MAX_TEXT_LENGTH + 1);
    if (text == NULL) {
        return NULL;  // Проверка на успешное выделение памяти
    }
    text[0] = '\0';  // Инициализируем пустой строкой
    
    int used[ARRAY_SIZE] = {0};
    int text_count = 0;
    
    // Создаем большую строку
    while (text_count < count && strlen(text) < MAX_TEXT_LENGTH) {
        int random = rand() % (max - min + 1) + min;
        
        if (used[random] == 0) {
            size_t current_len = strlen(text);
            size_t add_len = strlen(array[random]);
            
            if (current_len + add_len < MAX_TEXT_LENGTH) {
                strcat(text, array[random]);
                used[random] = 1;
                text_count++;
            } else {
                break;
            }
        }
    }
    return text;  
}

/**
 * @brief Основная функция CPU loader с упрощенной логикой
 *
 * Выполняет нагрузочное тестирование CPU путем вычисления MD5 хеша текста,
 * параллельно отслеживая загрузку процессора и количество процессов
 *
 * @param text Входной текст для хеширования
 * @param user_avg Указатель для возврата средней загрузки пользовательских процессов
 * @param system_avg Указатель для возврата средней загрузки системных процессов
 * @param wait_avg Указатель для возврата среднего времени ожидания ввода-вывода
 * @param context_switches_total Указатель для возврата общего количества переключений контекста
 * @param context_switches_delta Указатель для возврата изменения переключений контекста
 * @param max_parallel_processes Указатель для возврата максимального количества параллельных процессов
 * @return clock_t Время выполнения операции в тактах процессора
 */
clock_t cpu_loader(const char* text, double* user_avg, double* system_avg, double* wait_avg,
                   unsigned long* context_switches_total, unsigned long* context_switches_delta,
                   int* max_parallel_processes)
{  
    // Инициализация выходных параметров
    *user_avg = 0.0;
    *system_avg = 0.0;
    *wait_avg = 0.0;
    *context_switches_total = 0;
    *context_switches_delta = 0;
    *max_parallel_processes = 0;
    
    // Проверка входных данных
    if (!text) {
        *user_avg = *system_avg = *wait_avg = -1.0;
        return 1;
    }

    // Мониторинг процессов
    process_monitor_t process_monitor = {
        .monitoring = 1,
        .max_processes = 0
    };
    
    pthread_t process_monitor_thread_id;
    int monitor_created = 0;
    
    if (pthread_create(&process_monitor_thread_id, NULL, process_monitor_thread, &process_monitor) == 0) {
        monitor_created = 1;
    }
    
    // Мониторинг CPU
    pthread_t monitoring_thread;
    monitoring_result_t* monitoring_result = NULL;
    
    if (pthread_create(&monitoring_thread, NULL, cpu_monitoring, NULL) != 0) {
        // Если не удалось создать поток мониторинга, продолжаем без него
        monitoring_thread = 0;
    }
    
    // Основная работа - вычисление MD5
    char hash[33] = {0};
    clock_t start = clock();
    inefficient_md5(text, hash);
    clock_t end = clock();
    clock_t time_total = end - start;
    
    // Завершаем мониторинг процессов
    if (monitor_created) {
        process_monitor.monitoring = 0;
        pthread_join(process_monitor_thread_id, NULL);
        *max_parallel_processes = process_monitor.max_processes;
    }
    
    // Получаем результаты мониторинга CPU
    if (monitoring_thread) {
        pthread_join(monitoring_thread, (void**)&monitoring_result);
    }
    
    // Обрабатываем результаты мониторинга
    if (monitoring_result) {
        *user_avg = monitoring_result->user_avg;
        *system_avg = monitoring_result->system_avg;
        *wait_avg = monitoring_result->wait_avg;
        //*context_switches_total = monitoring_result->context_switches_total;
        *context_switches_delta = monitoring_result->context_switches_delta;
        free(monitoring_result);
    } else {
        *user_avg = *system_avg = *wait_avg = -1.0;
        *context_switches_total = *context_switches_delta = 0;
    }
    
    return time_total;
}

/**
 * @brief Упрощенная тестовая функция для проверки работы CPU loader
 *
 * Создает тестовый текст, запускает CPU loader и выводит результаты тестирования
 *
 * @return int 0 в случае успешного выполнения, 1 в случае ошибки
 */
int main_cpu() {
    printf("=== ТЕСТ CPU НАГРУЗЧИКА ===\n");
    
    char *test_text = create_text();
    if (test_text == NULL) {
        printf("Ошибка: не удалось создать тестовый текст\n");
        return 1;
    }
    
    double cpu_user, cpu_system, cpu_wait;
    unsigned long context_switches_total, context_switches_delta;
    int max_parallel_processes;
    
    clock_t cpu_time = cpu_loader(test_text, &cpu_user, &cpu_system, &cpu_wait, 
                                 &context_switches_total, &context_switches_delta,
                                 &max_parallel_processes);
    
    printf("\n=== РЕЗУЛЬТАТЫ ===\n");
    printf("Время выполнения: %ld тактов\n", cpu_time);
    printf("Загрузка CPU - user: %.1f%%, system: %.1f%%, wait: %.1f%%\n", 
           cpu_user, cpu_system, cpu_wait);
    printf("Переключения контекста: %lu\n", context_switches_delta);
    printf("Максимум параллельных процессов: %d\n", max_parallel_processes);
    
    free(test_text);
    
    return 0;
}