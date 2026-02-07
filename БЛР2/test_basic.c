/**
 * @file test_basic.c
 * @brief Минимальный тест vtpc
 */

#include "vtpc.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    printf("Минимальный тест vtpc:\n");
    
    const char *filename = "test.bin";
    unlink(filename);
    
    // 1. Открыть
    printf("1. vtpc_open... ");
    int fd = vtpc_open(filename, VTPC_O_RDWR, 0644);
    if (fd < 0) {
        printf("FAILED (%d)\n", fd);
        return 1;
    }
    printf("OK (fd=%d)\n", fd);
    
    // 2. Записать
    printf("2. vtpc_write... ");
    char write_data[] = "Test123";
    ssize_t w = vtpc_write(fd, write_data, strlen(write_data));
    if (w <= 0) {
        printf("FAILED (%zd)\n", w);
        vtpc_close(fd);
        return 1;
    }
    printf("OK (%zd bytes)\n", w);
    
    // 3. Вернуться в начало
    printf("3. vtpc_lseek... ");
    off_t pos = vtpc_lseek(fd, 0, SEEK_SET);
    if (pos != 0) {
        printf("FAILED (%ld)\n", pos);
        vtpc_close(fd);
        return 1;
    }
    printf("OK\n");
    
    // 4. Прочитать
    printf("4. vtpc_read... ");
    char read_data[128] = {0};
    ssize_t r = vtpc_read(fd, read_data, strlen(write_data));
    if (r <= 0) {
        printf("FAILED (%zd)\n", r);
        vtpc_close(fd);
        return 1;
    }
    printf("OK (%zd bytes: '%s')\n", r, read_data);
    
    // 5. Закрыть
    printf("5. vtpc_close... ");
    if (vtpc_close(fd) < 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("OK\n");
    
    // Проверка через стандартный cat
    printf("\nПроверка через cat:\n");
    system("cat test.bin && echo");
    
    vtpc_cleanup();
    unlink(filename);
    
    printf("\nВсе тесты пройдены!\n");
    return 0;
}