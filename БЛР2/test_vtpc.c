/**
 * @file test_vtpc_direct.c
 * @brief Тест vtpc с прямым доступом (O_DIRECT)
 */

#include "vtpc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>        // <-- ДОБАВЬТЕ ЭТО
#include <sys/stat.h>     /

#define ALIGNMENT 512

int main() {
    printf("=== Тест vtpc с O_DIRECT ===\n");
    
    const char *test_file = "./test_vtpc_direct.bin";
    unlink(test_file); // Удаляем старый файл
    
    // 1. Создаем файл стандартными средствами
    printf("1. Создаем файл стандартными средствами...\n");
    int std_fd = open(test_file, O_CREAT | O_RDWR | O_DIRECT, 0644);
    if (std_fd < 0) {
        perror("open O_DIRECT");
        printf("Пробуем без O_DIRECT для создания...\n");
        std_fd = open(test_file, O_CREAT | O_RDWR, 0644);
        if (std_fd < 0) {
            perror("open");
            return 1;
        }
    }
    
    // Расширяем файл до размера, кратного 512
    if (ftruncate(std_fd, ALIGNMENT) < 0) {
        perror("ftruncate");
        close(std_fd);
        return 1;
    }
    close(std_fd);
    
    // 2. Открываем через vtpc с O_DIRECT
    printf("2. Открываем через vtpc с O_DIRECT...\n");
    int fd = vtpc_open(test_file, VTPC_O_RDWR | VTPC_O_DIRECT, 0644);
    if (fd < 0) {
        printf("ОШИБКА: vtpc_open вернул %d\n", fd);
        // Пробуем без O_DIRECT
        fd = vtpc_open(test_file, VTPC_O_RDWR, 0644);
        if (fd < 0) {
            printf("ОШИБКА: не удалось открыть файл\n");
            return 1;
        }
        printf("  Открыто БЕЗ O_DIRECT (fd=%d)\n", fd);
    } else {
        printf("  Открыто с O_DIRECT (fd=%d)\n", fd);
    }
    
    // 3. Создаем выровненный буфер
    printf("3. Создаем выровненный буфер...\n");
    char *aligned_buffer = NULL;
    if (posix_memalign((void**)&aligned_buffer, ALIGNMENT, ALIGNMENT)) {
        perror("posix_memalign");
        vtpc_close(fd);
        return 1;
    }
    
    // 4. Записываем данные (кратно 512)
    printf("4. Записываем данные...\n");
    const char *test_data = "Hello, World!";
    size_t data_len = strlen(test_data);
    
    // Копируем данные в выровненный буфер
    memset(aligned_buffer, 0, ALIGNMENT);
    memcpy(aligned_buffer, test_data, data_len);
    
    // Записываем через vtpc
    ssize_t written = vtpc_write(fd, aligned_buffer, ALIGNMENT);
    if (written <= 0) {
        printf("ОШИБКА: vtpc_write вернул %zd\n", written);
        free(aligned_buffer);
        vtpc_close(fd);
        return 1;
    }
    printf("  Записано %zd байт (ожидалось %d)\n", written, ALIGNMENT);
    
    // 5. Синхронизируем
    printf("5. Синхронизируем...\n");
    if (vtpc_fsync(fd) < 0) {
        printf("ОШИБКА: vtpc_fsync\n");
    }
    
    // 6. Возвращаемся в начало
    printf("6. Возвращаемся в начало...\n");
    off_t pos = vtpc_lseek(fd, 0, SEEK_SET);
    printf("  Позиция: %ld\n", pos);
    
    // 7. Читаем данные
    printf("7. Читаем данные...\n");
    char *read_buffer = NULL;
    if (posix_memalign((void**)&read_buffer, ALIGNMENT, ALIGNMENT)) {
        perror("posix_memalign для чтения");
        free(aligned_buffer);
        vtpc_close(fd);
        return 1;
    }
    memset(read_buffer, 0, ALIGNMENT);
    
    ssize_t read_bytes = vtpc_read(fd, read_buffer, ALIGNMENT);
    printf("  Прочитано %zd байт\n", read_bytes);
    
    // 8. Сравниваем
    printf("8. Сравниваем...\n");
    if (read_bytes >= data_len && memcmp(aligned_buffer, read_buffer, data_len) == 0) {
        printf("  ✓ Первые %zu байт совпадают: '%.*s'\n", 
               data_len, (int)data_len, read_buffer);
    } else {
        printf("  ✗ Данные не совпадают\n");
        printf("    Записано: '%.*s'\n", (int)data_len, aligned_buffer);
        printf("    Прочитано: '%.*s'\n", (int)data_len, read_buffer);
    }
    
    // 9. Показываем hexdump
    printf("\n9. Hexdump файла:\n");
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "hexdump -C -n 64 %s", test_file);
    system(cmd);
    
    // 10. Очистка
    printf("\n10. Очистка...\n");
    free(aligned_buffer);
    free(read_buffer);
    vtpc_close(fd);
    vtpc_cleanup();
    
    // Не удаляем файл для проверки
    printf("\nФайл сохранен: %s\n", test_file);
    printf("Проверьте: hexdump -C %s | head -20\n", test_file);
    
    return 0;
}