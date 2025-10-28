#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <openssl/md5.h>

#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150
#define MAX_TEXT_LENGTH 500000


typedef struct {
    double user;   // %user
    double system; // %sys  
    double wait;   // %wait
} cpu_stats_t;


typedef struct {
    double user_avg;
    double system_avg;
    double wait_avg;
} monitoring_result_t;

// Получаем полную статистику CPU
cpu_stats_t get_cpu_stats_from_top() {
    cpu_stats_t stats = {-1.0, -1.0, -1.0};
    
    // Получаем полную строку статистики CPU
    FILE* top = popen("top -bn1 | grep 'Cpu(s)'", "r");
    if (!top) {
        return stats;
    }
    
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), top)) {
        // Парсим строку типа: "%Cpu(s):  12.3 us,  5.6 sy,  0.0 ni, 82.1 id,  3.2 wa,  0.0 hi,  0.0 si,  0.0 st"
        double us, sy, ni, id, wa, hi, si, st;
        if (sscanf(buffer, "%%Cpu(s): %lf us, %lf sy, %lf ni, %lf id, %lf wa, %lf hi, %lf si, %lf st", 
                  &us, &sy, &ni, &id, &wa, &hi, &si, &st) == 8) {
            stats.user = us;
            stats.system = sy;
            stats.wait = wa;
        }
    }
    
    pclose(top);
    return stats;
}

// Данная функция нужна для запуска в параллельном потоке мониторинга утилит cpu в время работы данного нагрузчика
// Измерять до работы нагрузчика и после невозможно, ведь так нельзя будет узнать точное значение
void* cpu_monitoring(void* arg){
    cpu_stats_t* result = malloc(sizeof(cpu_stats_t)); // Вдыелим память
    if (!result) return NULL;

    double total_user = 0.0, total_wait = 0.0, total_sys = 0.0;
    int valis_samples = 0; // Позволит проверить, что есть замеры, которые прошли валидно
    
    for (int i = 0; i < 10; i++){
        cpu_stats_t monitor = get_cpu_stats_from_top();

        if (monitor.user >= 0 && monitor.system >= 0 && monitor.wait >= 0) {
            total_user += monitor.user;
            total_wait += monitor.wait;
            total_sys += monitor.system;
            valis_samples++;
        }
    }

    if (valis_samples > 0){
        result->system = total_sys/valis_samples;
        result->user = total_user/valis_samples;
        result->wait = total_wait/valis_samples;
    }

    return result;

}


void inefficient_md5(const char* text, char* hash) {
    MD5_CTX context;
    unsigned char digest[MD5_DIGEST_LENGTH];
    char large_buffer[500000];
    
    // ОЧЕНЬ многократное вычисление MD5
    for (int iteration = 0; iteration < 100; iteration++) {
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
    return text;  // Теперь это валидный указатель на heap
}

clock_t cpu_loader(const char* text, double* user_avg, double* system_avg, double* wait_avg) {
    char hash[33];
    clock_t start = clock();
    
    // Создаем поток для мониторинга
    pthread_t monitoring_thread;
    monitoring_result_t* monitoring_result;

    if (pthread_create(&monitoring_thread, NULL, cpu_monitoring, NULL) != 0) {
        // Ошибка создания потока
        *user_avg = *system_avg = *wait_avg = -1.0;
        return 1;
    }

    // Основная нагрузка
    inefficient_md5(text, hash);
    
    // Получаем результаты мониторинга
    pthread_join(monitoring_thread, (void**)&monitoring_result);
    
    clock_t end = clock();
    clock_t time_total = end - start;
    
    // Записываем результаты в переданные указатели
    if (monitoring_result) {
        *user_avg = monitoring_result->user_avg;
        *system_avg = monitoring_result->system_avg;
        *wait_avg = monitoring_result->wait_avg;
        free(monitoring_result);
    } else {
        *user_avg = *system_avg = *wait_avg = -1.0;
    }
    
    return time_total;
}