#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/md5.h>

#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150
#define MAX_TEXT_LENGTH 500000

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



#include <stdlib.h>
#include <string.h>

char* create_text() {
    int array_size = ARRAY_SIZE;
    char array[ARRAY_SIZE][MAX_NUM_LENGTH];
    int count = 0;
    
    FILE* ptr = fopen("fragments_text.txt", "r");
    if (ptr == NULL) {
        // printf("Ошибка: файл fragments_text.txt не найден!\n");
        return NULL;  // ← Исправлено: NULL вместо 0
    }
    
    while (count < array_size && fscanf(ptr, "%149s", array[count]) == 1) {
        count++;
    }
    fclose(ptr);
    
    if (count == 0) {
        // printf("Файл пуст!\n");
        return NULL;  // ← Исправлено: NULL вместо 0
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

clock_t cpu_loader(const char* text) {
    //printf("Поток CPU начал работу\n");
    
    
    
    // printf("Поток создал строку из %d фрагментов (%zu символов)\n", 
    //        text_count, strlen(text));
    
    char hash[33];
    clock_t start = clock();
    
    // Основная нагрузка
    inefficient_md5(text, hash);
    
    clock_t end = clock();
    clock_t time_total = end - start;
    
    //printf("Поток завершил работу за %ld мс\n", time_total);
    return time_total;
}