// vtpc_raw.c - полный код БЕЗ libc оберток
#include "vtpc.h"
#include <linux/fcntl.h>
#include <sys/syscall.h>
#include <stdatomic.h>
#include <errno.h>
#include <sys/mman.h>

// Определяем недостающие константы вручную, если они не определены
#ifndef PROT_READ
#define PROT_READ   0x1
#endif

#ifndef PROT_WRITE
#define PROT_WRITE  0x2
#endif

#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0x02
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#ifndef SEEK_END
#define SEEK_END 2
#endif

// CLOCK_REALTIME определен в <time.h>, но если его нет, определим вручную
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

// Номера системных вызовов для x86_64
#ifndef SYS_open
#define SYS_open 2
#endif

#ifndef SYS_close
#define SYS_close 3
#endif

#ifndef SYS_read
#define SYS_read 0
#endif

#ifndef SYS_write
#define SYS_write 1
#endif

#ifndef SYS_lseek
#define SYS_lseek 8
#endif

#ifndef SYS_fsync
#define SYS_fsync 74
#endif

#ifndef SYS_mmap
#define SYS_mmap 9
#endif

#ifndef SYS_munmap
#define SYS_munmap 11
#endif

#ifndef SYS_clock_gettime
#define SYS_clock_gettime 228
#endif

// Размеры
#define BLOCK_SIZE 4096
#define MAX_CACHE_BLOCKS 1024

// ========== СИСТЕМНЫЕ ВЫЗОВЫ НАПРЯМУЮ ==========

// Объявляем syscall, чтобы избежать предупреждения
long syscall(long number, ...);

static inline int raw_open(const char *path, int flags) {
    return syscall(SYS_open, path, flags, 0666);
}

static inline int raw_close(int fd) {
    return syscall(SYS_close, fd);
}

static inline ssize_t raw_read(int fd, void *buf, size_t count) {
    return syscall(SYS_read, fd, buf, count);
}

static inline ssize_t raw_write(int fd, const void *buf, size_t count) {
    return syscall(SYS_write, fd, buf, count);
}

static inline off_t raw_lseek(int fd, off_t offset, int whence) {
    return syscall(SYS_lseek, fd, offset, whence);
}

static inline int raw_fsync(int fd) {
    return syscall(SYS_fsync, fd);
}

static inline void* raw_mmap(size_t size) {
    void* result = (void*)syscall(SYS_mmap, 
                         NULL, size, 
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, 
                         -1, 0);
    return result;
}

static inline int raw_munmap(void *addr, size_t size) {
    return syscall(SYS_munmap, addr, size);
}

static inline time_t raw_time(void) {
    struct timespec ts;
    syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
    return ts.tv_sec;
}

// ========== СВОИ РЕАЛИЗАЦИИ БАЗОВЫХ ФУНКЦИЙ ==========

static void raw_memset(void *ptr, int value, size_t num) {
    unsigned char *p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
}

static void raw_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

// ========== СТРУКТУРЫ ДАННЫХ ==========

typedef struct CacheBlock {
    int fd;
    off_t block_number;
    char data[BLOCK_SIZE];
    int dirty;
    time_t last_access;
    struct CacheBlock *prev;
    struct CacheBlock *next;
} CacheBlock;

typedef struct FileInfo {
    int fd;
    off_t file_size;
    off_t position;
} FileInfo;

typedef struct CacheManager {
    CacheBlock *head;
    CacheBlock *tail;
    int block_count;
    volatile int lock;
} CacheManager;

static FileInfo *open_files[1024];
static CacheManager cache_manager;

// ========== СПИНЛОКИ ==========

static inline void spin_lock(volatile int *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        #ifdef __x86_64__
            __asm__ __volatile__("pause" ::: "memory");
        #endif
    }
}

static inline void spin_unlock(volatile int *lock) {
    __sync_lock_release(lock);
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ==========

static off_t get_block_number(off_t position) {
    return position / BLOCK_SIZE;
}

static off_t get_block_offset(off_t position) {
    return position % BLOCK_SIZE;
}

// ========== ОСНОВНЫЕ ФУНКЦИИ КЭША ==========

static void read_block_from_disk(CacheBlock *block) {
    FileInfo *file_info = open_files[block->fd];
    if (!file_info) return;
    
    off_t pos = block->block_number * BLOCK_SIZE;
    off_t old_pos = raw_lseek(file_info->fd, 0, SEEK_CUR);
    
    if (old_pos < 0 || raw_lseek(file_info->fd, pos, SEEK_SET) < 0) {
        if (old_pos >= 0) raw_lseek(file_info->fd, old_pos, SEEK_SET);
        raw_memset(block->data, 0, BLOCK_SIZE);
        return;
    }
    
    ssize_t bytes = raw_read(file_info->fd, block->data, BLOCK_SIZE);
    raw_lseek(file_info->fd, old_pos, SEEK_SET);
    
    if (bytes < BLOCK_SIZE) {
        if (bytes > 0) {
            raw_memset(block->data + bytes, 0, BLOCK_SIZE - bytes);
        } else {
            raw_memset(block->data, 0, BLOCK_SIZE);
        }
    }
    
    block->dirty = 0;
    block->last_access = raw_time();
}

static void write_block_to_disk(CacheBlock *block) {
    FileInfo *file_info = open_files[block->fd];
    if (!file_info || !block->dirty) return;
    
    off_t pos = block->block_number * BLOCK_SIZE;
    off_t old_pos = raw_lseek(file_info->fd, 0, SEEK_CUR);
    
    if (old_pos < 0 || raw_lseek(file_info->fd, pos, SEEK_SET) < 0) {
        if (old_pos >= 0) raw_lseek(file_info->fd, old_pos, SEEK_SET);
        return;
    }
    
    ssize_t written = raw_write(file_info->fd, block->data, BLOCK_SIZE);
    raw_lseek(file_info->fd, old_pos, SEEK_SET);
    
    if (written == BLOCK_SIZE) {
        block->dirty = 0;
    }
}

static CacheBlock* find_block_in_cache(int fd, off_t block_number) {
    CacheBlock *current = cache_manager.head;
    
    while (current != NULL) {
        if (current->fd == fd && current->block_number == block_number) {
            // Обновляем время доступа
            current->last_access = raw_time();
            
            // Перемещаем в начало списка (LRU)
            if (current != cache_manager.head) {
                // Вынимаем из текущей позиции
                if (current->prev) {
                    current->prev->next = current->next;
                }
                if (current->next) {
                    current->next->prev = current->prev;
                }
                
                // Обновляем хвост если нужно
                if (current == cache_manager.tail) {
                    cache_manager.tail = current->prev;
                }
                
                // Вставляем в начало
                current->prev = NULL;
                current->next = cache_manager.head;
                
                if (cache_manager.head) {
                    cache_manager.head->prev = current;
                }
                
                cache_manager.head = current;
                
                // Если список был пуст
                if (cache_manager.tail == NULL) {
                    cache_manager.tail = current;
                }
            }
            
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

static void evict_lru_block(void) {
    if (cache_manager.tail == NULL) {
        return;
    }
    
    CacheBlock *to_evict = cache_manager.tail;
    
    // Если блок "грязный", записываем его на диск
    if (to_evict->dirty) {
        write_block_to_disk(to_evict);
    }
    
    // Удаляем блок из списка
    if (to_evict->prev) {
        to_evict->prev->next = NULL;
    }
    
    cache_manager.tail = to_evict->prev;
    
    if (cache_manager.head == to_evict) {
        cache_manager.head = NULL;
    }
    
    // Освобождаем память
    raw_munmap(to_evict, sizeof(CacheBlock));
    cache_manager.block_count--;
}

static CacheBlock* allocate_new_block(int fd, off_t block_number) {
    spin_lock(&cache_manager.lock);
    
    // Если кэш полон, вытесняем LRU блок
    if (cache_manager.block_count >= MAX_CACHE_BLOCKS) {
        evict_lru_block();
    }
    
    // Создаем новый блок
    CacheBlock *new_block = raw_mmap(sizeof(CacheBlock));
    if (!new_block) {
        spin_unlock(&cache_manager.lock);
        return NULL;
    }
    
    // Инициализируем блок
    raw_memset(new_block, 0, sizeof(CacheBlock));
    new_block->fd = fd;
    new_block->block_number = block_number;
    new_block->last_access = raw_time();
    
    // Читаем данные с диска
    read_block_from_disk(new_block);
    
    // Добавляем блок в начало списка
    new_block->next = cache_manager.head;
    if (cache_manager.head) {
        cache_manager.head->prev = new_block;
    }
    
    cache_manager.head = new_block;
    
    if (cache_manager.tail == NULL) {
        cache_manager.tail = new_block;
    }
    
    cache_manager.block_count++;
    
    spin_unlock(&cache_manager.lock);
    return new_block;
}

// ========== API ФУНКЦИИ ==========

int vtpc_open(const char *path) {
    static int initialized = 0;
    if (!initialized) {
        raw_memset(&cache_manager, 0, sizeof(cache_manager));
        raw_memset(open_files, 0, sizeof(open_files));
        initialized = 1;
    }
    
    // Пробуем открыть с O_RDWR (2)
    int sys_fd = raw_open(path, 2);
    if (sys_fd < 0) {
        sys_fd = raw_open(path, 0); // Пробуем O_RDONLY (0)
        if (sys_fd < 0) return -1;
    }
    
    // Получаем размер файла
    off_t size = raw_lseek(sys_fd, 0, SEEK_END);
    if (size < 0 || raw_lseek(sys_fd, 0, SEEK_SET) < 0) {
        raw_close(sys_fd);
        return -1;
    }
    
    // Ищем свободный слот
    int vtpc_fd = -1;
    for (int i = 0; i < 1024; i++) {
        if (!open_files[i]) {
            vtpc_fd = i;
            break;
        }
    }
    
    if (vtpc_fd < 0) {
        raw_close(sys_fd);
        return -1;
    }
    
    // Выделяем память
    FileInfo *info = raw_mmap(sizeof(FileInfo));
    if (!info) {
        raw_close(sys_fd);
        return -1;
    }
    
    info->fd = sys_fd;
    info->file_size = size;
    info->position = 0;
    
    open_files[vtpc_fd] = info;
    return vtpc_fd;
}

int vtpc_close(int fd) {
    if (fd < 0 || fd >= 1024 || !open_files[fd]) return -1;
    
    spin_lock(&cache_manager.lock);
    
    // Пишем dirty блоки
    CacheBlock *curr = cache_manager.head;
    while (curr) {
        if (curr->fd == fd && curr->dirty) {
            write_block_to_disk(curr);
        }
        curr = curr->next;
    }
    
    // Удаляем блоки из кэша
    curr = cache_manager.head;
    while (curr) {
        CacheBlock *next = curr->next;
        if (curr->fd == fd) {
            // Удаляем из списка
            if (curr->prev) curr->prev->next = curr->next;
            if (curr->next) curr->next->prev = curr->prev;
            if (curr == cache_manager.head) cache_manager.head = curr->next;
            if (curr == cache_manager.tail) cache_manager.tail = curr->prev;
            
            raw_munmap(curr, sizeof(CacheBlock));
            cache_manager.block_count--;
        }
        curr = next;
    }
    
    spin_unlock(&cache_manager.lock);
    
    // Закрываем файл и освобождаем память
    FileInfo *info = open_files[fd];
    raw_close(info->fd);
    raw_munmap(info, sizeof(FileInfo));
    open_files[fd] = 0;
    
    return 0;
}

ssize_t vtpc_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= 1024 || !open_files[fd] || !buf) return -1;
    
    FileInfo *info = open_files[fd];
    if (info->position >= info->file_size) return 0;
    
    size_t to_read = count;
    if (info->position + (off_t)to_read > info->file_size) {
        to_read = info->file_size - info->position;
    }
    
    size_t total = 0;
    char *buffer = (char*)buf;
    
    while (total < to_read) {
        off_t pos = info->position + (off_t)total;
        off_t block_num = get_block_number(pos);
        off_t offset = get_block_offset(pos);
        
        size_t in_block = BLOCK_SIZE - offset;
        size_t needed = to_read - total;
        size_t copy = (in_block < needed) ? in_block : needed;
        
        // Ищем блок в кэше
        CacheBlock *block = find_block_in_cache(fd, block_num);
        
        if (!block) {
            block = allocate_new_block(fd, block_num);
            if (!block) {
                info->position += (off_t)total;
                return total;
            }
        }
        
        raw_memcpy(buffer + total, block->data + offset, copy);
        total += copy;
    }
    
    info->position += (off_t)total;
    return total;
}

ssize_t vtpc_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= 1024 || !open_files[fd] || !buf) return -1;
    
    FileInfo *info = open_files[fd];
    size_t total_bytes_written = 0;
    const char *buffer = (const char*)buf;
    
    while (total_bytes_written < count) {
        off_t current_pos = info->position + (off_t)total_bytes_written;
        off_t block_num = get_block_number(current_pos);
        off_t block_offset = get_block_offset(current_pos);
        
        size_t bytes_in_block = BLOCK_SIZE - block_offset;
        size_t bytes_needed = count - total_bytes_written;
        size_t bytes_to_copy = (bytes_in_block < bytes_needed) ? bytes_in_block : bytes_needed;
        
        // Ищем блок в кэше
        CacheBlock *block = find_block_in_cache(fd, block_num);
        
        // Если блока нет в кэше, создаем новый
        if (!block) {
            block = allocate_new_block(fd, block_num);
            if (!block) {
                info->position += (off_t)total_bytes_written;
                return total_bytes_written;
            }
        }
        
        // Копируем данные из буфера в блок
        raw_memcpy(block->data + block_offset,
                   buffer + total_bytes_written,
                   bytes_to_copy);
        
        // Помечаем блок как "грязный"
        block->dirty = 1;
        block->last_access = raw_time();
        
        // Перемещаем блок в начало списка LRU
        if (block != cache_manager.head) {
            // Вынимаем из текущей позиции
            if (block->prev) block->prev->next = block->next;
            if (block->next) block->next->prev = block->prev;
            
            if (block == cache_manager.tail) {
                cache_manager.tail = block->prev;
            }
            
            // Вставляем в начало
            block->prev = NULL;
            block->next = cache_manager.head;
            
            if (cache_manager.head) {
                cache_manager.head->prev = block;
            }
            
            cache_manager.head = block;
            
            if (cache_manager.tail == NULL) {
                cache_manager.tail = block;
            }
        }
        
        total_bytes_written += bytes_to_copy;
        
        // Обновляем размер файла, если нужно
        off_t new_end_pos = current_pos + (off_t)bytes_to_copy;
        if (new_end_pos > info->file_size) {
            info->file_size = new_end_pos;
        }
    }
    
    info->position += (off_t)total_bytes_written;
    return total_bytes_written;
}

off_t vtpc_lseek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= 1024 || !open_files[fd]) return -1;
    
    FileInfo *info = open_files[fd];
    off_t new_position;
    
    switch (whence) {
        case SEEK_SET:
            new_position = offset;
            break;
            
        case SEEK_CUR:
            new_position = info->position + offset;
            break;
            
        case SEEK_END:
            new_position = info->file_size + offset;
            break;
            
        default:
            return -1;
    }
    
    // Проверяем, что позиция не отрицательна
    if (new_position < 0) {
        return -1;
    }
    
    info->position = new_position;
    return new_position;
}

int vtpc_fsync(int fd) {
    if (fd < 0 || fd >= 1024 || !open_files[fd]) return -1;
    
    spin_lock(&cache_manager.lock);
    
    // Записываем все "грязные" блоки этого файла на диск
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->fd == fd && current->dirty) {
            write_block_to_disk(current);
        }
        current = current->next;
    }
    
    spin_unlock(&cache_manager.lock);
    
    // Вызываем системный fsync для гарантированной записи на диск
    FileInfo *info = open_files[fd];
    int result = raw_fsync(info->fd);
    
    return result;
}

// ========== ФУНКЦИЯ ОЧИСТКИ ==========

void vtpc_cleanup(void) {
    spin_lock(&cache_manager.lock);
    
    // Записываем все "грязные" блоки на диск
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->dirty) {
            write_block_to_disk(current);
        }
        CacheBlock *next = current->next;
        raw_munmap(current, sizeof(CacheBlock));
        current = next;
    }
    
    cache_manager.head = NULL;
    cache_manager.tail = NULL;
    cache_manager.block_count = 0;
    
    spin_unlock(&cache_manager.lock);
    
    // Закрываем все открытые файлы
    for (int i = 0; i < 1024; i++) {
        if (open_files[i] != NULL) {
            FileInfo *info = open_files[i];
            raw_close(info->fd);
            raw_munmap(info, sizeof(FileInfo));
            open_files[i] = NULL;
        }
    }
}

// ========== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ ОТЛАДКИ ==========

#ifdef DEBUG
size_t vtpc_get_cache_size(void) {
    return cache_manager.block_count;
}

size_t vtpc_get_dirty_count(void) {
    spin_lock(&cache_manager.lock);
    
    size_t dirty_count = 0;
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->dirty) {
            dirty_count++;
        }
        current = current->next;
    }
    
    spin_unlock(&cache_manager.lock);
    return dirty_count;
}

void vtpc_print_cache_stats(void) {
    spin_lock(&cache_manager.lock);
    
    size_t dirty_count = 0;
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->dirty) {
            dirty_count++;
        }
        current = current->next;
    }
    
    spin_unlock(&cache_manager.lock);
}
#endif