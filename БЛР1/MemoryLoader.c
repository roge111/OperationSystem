#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_SIZE 10000
#define MAX_NUM_LENGTH 150

clock_t memory_loader(int number) {
    //printf("Программа начала работу\n");
    
    // Объявляем переменные
    int array_size = ARRAY_SIZE;
    char array[ARRAY_SIZE][MAX_NUM_LENGTH]; // Массив для хранения строк
    int count = 0;
    
    // Открываем файл для чтения
    FILE* ptr = fopen("fragments_numbers.txt", "r");
    
    if (ptr == NULL) {
        //printf("Ошибка при открытии файла!\n");
        exit(1);
    }
    
    // Замеряем время чтения
    clock_t start_read = clock();
    
    // Читаем числа из файла
    while (count < array_size && 
           fscanf(ptr, "%149s", array[count]) == 1) { 
        count++;
    }
    
    fclose(ptr);
    clock_t end_read = clock();
    
    // Преобразуем число в строку
    char number_str[MAX_NUM_LENGTH];
    sprintf(number_str, "%d", number);
    
    // Ищем и заменяем первое вхождение числа
    for (int i = 0; i < count; i++) {
        if (atoi(array[i]) == number) {
            strcpy(array[i], number_str);
            break;
        }
    }
    
    // Замеряем время записи
    clock_t start_write = clock();
    
    // Открываем файл для записи
    ptr = fopen("output.txt", "w");
    if (ptr == NULL) {
        //printf("Ошибка при создании файла для записи!\n");
        exit(1);
    }
    
    // Записываем данные
    for (int i = 0; i < count; i++) {
        fprintf(ptr, "%s\n", array[i]);
    }
    
    fclose(ptr);
    clock_t end_write = clock();
    
    // Вычисляем время
    clock_t time_read = end_read - start_read;
    clock_t time_write = end_write - start_write;
    clock_t time_total = time_read + time_write;
    
    return time_total;
}

// Пример функции main для тестирования
int main_memory() {
    int number;
    
    //printf("Введите число для замены: ");
    if (scanf("%d", &number) != 1) {
        //printf("Ошибка ввода числа\n");
        return 1;
    }
    
    clock_t total_time = memory_loader(number);
    //printf("Общее время работы: %ld тактов\n", total_time);
    
    return 0;
}
