#ifndef VTPC_H
#define VTPC_H

#include <stddef.h>
#include <sys/types.h>

// Основные функции API
int vtpc_open(const char *path);
int vtpc_close(int fd);
ssize_t vtpc_read(int fd, void *buf, size_t count);
ssize_t vtpc_write(int fd, const void *buf, size_t count);
off_t vtpc_lseek(int fd, off_t offset, int whence);
int vtpc_fsync(int fd);

// Дополнительные функции (опционально)
void vtpc_cleanup(void);  // Для явной очистки ресурсов

#endif // VTPC_H