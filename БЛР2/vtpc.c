// vtpc.c
#include "vtpc.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>

// Определяем макросы, если они не определены
#ifndef O_DIRECT
#define O_DIRECT 0  // Если не поддерживается, игнорируем
#endif

#ifndef O_NOATIME
#define O_NOATIME 0  // Если не поддерживается, игнорируем
#endif

// Размер блока (обычно соответствует размеру страницы или сектору диска)
#define BLOCK_SIZE 4096

// Максимальное количество блоков в кэше
#define MAX_CACHE_BLOCKS 1024

// Структура для хранения информации о блоке
typedef struct CacheBlock {
    int fd;                 // Файловый дескриптор
    off_t block_number;     // Номер блока в файле
    char data[BLOCK_SIZE];  // Данные блока
    int dirty;              // Флаг "грязного" блока (изменён, но не записан)
    time_t last_access;     // Время последнего доступа (для LRU)
    struct CacheBlock *prev;
    struct CacheBlock *next;
} CacheBlock;

// Структура для хранения информации об открытом файле
typedef struct FileInfo {
    int fd;                 // Системный файловый дескриптор
    off_t file_size;        // Размер файла
    off_t position;         // Текущая позиция в файле
} FileInfo;

// Структура для управления кэшем
typedef struct CacheManager {
    CacheBlock *head;       // Начало списка LRU (самый недавно использованный)
    CacheBlock *tail;       // Конец списка LRU (самый давно использованный)
    int block_count;        // Текущее количество блоков в кэше
    pthread_mutex_t lock;   // Мьютекс для потокобезопасности
} CacheManager;

// Глобальные переменные
static FileInfo *open_files[1024];  // Массив открытых файлов
static CacheManager cache_manager;  // Менеджер кэша

// Вспомогательные функции
static CacheBlock* find_block_in_cache(int fd, off_t block_number);
static CacheBlock* allocate_new_block(int fd, off_t block_number);
static void read_block_from_disk(CacheBlock *block);
static void write_block_to_disk(CacheBlock *block);
static void move_to_front(CacheBlock *block);
static void evict_lru_block(void);
static off_t get_block_number(off_t position);
static off_t get_block_offset(off_t position);

// Инициализация кэша
static void init_cache(void) {
    cache_manager.head = NULL;
    cache_manager.tail = NULL;
    cache_manager.block_count = 0;
    pthread_mutex_init(&cache_manager.lock, NULL);
    
    // Инициализируем массив открытых файлов
    for (int i = 0; i < 1024; i++) {
        open_files[i] = NULL;
    }
}

// Освобождение ресурсов кэша
static void cleanup_cache(void) {
    pthread_mutex_lock(&cache_manager.lock);
    
    // Записываем все "грязные" блоки на диск
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->dirty) {
            write_block_to_disk(current);
        }
        CacheBlock *next = current->next;
        free(current);
        current = next;
    }
    
    cache_manager.head = NULL;
    cache_manager.tail = NULL;
    cache_manager.block_count = 0;
    
    pthread_mutex_unlock(&cache_manager.lock);
    pthread_mutex_destroy(&cache_manager.lock);
}

// Поиск блока в кэше
static CacheBlock* find_block_in_cache(int fd, off_t block_number) {
    CacheBlock *current = cache_manager.head;
    
    while (current != NULL) {
        if (current->fd == fd && current->block_number == block_number) {
            // Обновляем время доступа
            current->last_access = time(NULL);
            move_to_front(current);
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

// Перемещение блока в начало списка (самый недавно использованный)
static void move_to_front(CacheBlock *block) {
    if (block == cache_manager.head) {
        return; // Уже в начале
    }
    
    // Удаляем блок из текущей позиции
    if (block->prev) {
        block->prev->next = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    
    // Если блок был хвостом, обновляем хвост
    if (block == cache_manager.tail) {
        cache_manager.tail = block->prev;
    }
    
    // Вставляем блок в начало
    block->prev = NULL;
    block->next = cache_manager.head;
    
    if (cache_manager.head) {
        cache_manager.head->prev = block;
    }
    
    cache_manager.head = block;
    
    // Если список был пуст, блок становится и хвостом
    if (cache_manager.tail == NULL) {
        cache_manager.tail = block;
    }
}

// Вытеснение самого давно неиспользуемого блока (LRU)
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
    
    free(to_evict);
    cache_manager.block_count--;
}

// Вычисление номера блока для заданной позиции
static off_t get_block_number(off_t position) {
    return position / BLOCK_SIZE;
}

// Вычисление смещения внутри блока
static off_t get_block_offset(off_t position) {
    return position % BLOCK_SIZE;
}

// Чтение блока с диска
static void read_block_from_disk(CacheBlock *block) {
    FileInfo *file_info = open_files[block->fd];
    if (file_info == NULL) {
        return;
    }
    
    off_t position = block->block_number * BLOCK_SIZE;
    
    // Используем pread для атомарного чтения без изменения позиции
    ssize_t bytes_read = pread(file_info->fd, block->data, BLOCK_SIZE, position);
    
    if (bytes_read < BLOCK_SIZE) {
        // Если прочитали меньше, чем блок, заполняем остаток нулями
        if (bytes_read > 0) {
            memset(block->data + bytes_read, 0, BLOCK_SIZE - bytes_read);
        } else {
            memset(block->data, 0, BLOCK_SIZE);
        }
    }
    
    block->dirty = 0;
    block->last_access = time(NULL);
}

// Запись блока на диск
static void write_block_to_disk(CacheBlock *block) {
    FileInfo *file_info = open_files[block->fd];
    if (file_info == NULL || !block->dirty) {
        return;
    }
    
    off_t position = block->block_number * BLOCK_SIZE;
    
    // Используем pwrite для атомарной записи без изменения позиции
    ssize_t written = pwrite(file_info->fd, block->data, BLOCK_SIZE, position);
    
    if (written != BLOCK_SIZE) {
        // Ошибка записи - можно добавить логирование
        fprintf(stderr, "Warning: Failed to write full block to disk\n");
    }
    
    block->dirty = 0;
}

// Выделение нового блока в кэше
static CacheBlock* allocate_new_block(int fd, off_t block_number) {
    pthread_mutex_lock(&cache_manager.lock);
    
    // Если кэш полон, вытесняем LRU блок
    if (cache_manager.block_count >= MAX_CACHE_BLOCKS) {
        evict_lru_block();
    }
    
    // Создаем новый блок
    CacheBlock *new_block = (CacheBlock*)malloc(sizeof(CacheBlock));
    if (new_block == NULL) {
        pthread_mutex_unlock(&cache_manager.lock);
        return NULL;
    }
    
    // Инициализируем блок
    new_block->fd = fd;
    new_block->block_number = block_number;
    new_block->dirty = 0;
    new_block->last_access = time(NULL);
    new_block->prev = NULL;
    new_block->next = NULL;
    
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
    
    pthread_mutex_unlock(&cache_manager.lock);
    return new_block;
}

// Открытие файла
int vtpc_open(const char *path) {
    // Статическая инициализация кэша при первом вызове
    static int initialized = 0;
    if (!initialized) {
        init_cache();
        initialized = 1;
    }
    
    // Пытаемся открыть файл с прямым доступом, если поддерживается
    int sys_fd = -1;
    int flags = O_RDWR;
    
    // Пробуем добавить O_DIRECT, если определен
    if (O_DIRECT != 0) {
        sys_fd = open(path, O_RDWR | O_DIRECT);
    }
    
    // Если O_DIRECT не поддерживается или не сработал, открываем без него
    if (sys_fd < 0) {
        sys_fd = open(path, O_RDWR);
        if (sys_fd < 0) {
            // Если не удалось открыть для чтения/записи, пробуем только для чтения
            sys_fd = open(path, O_RDONLY);
            if (sys_fd < 0) {
                return -1;
            }
        }
    }
    
    // Получаем размер файла
    off_t file_size = lseek(sys_fd, 0, SEEK_END);
    if (file_size < 0) {
        close(sys_fd);
        return -1;
    }
    
    if (lseek(sys_fd, 0, SEEK_SET) < 0) {
        close(sys_fd);
        return -1;
    }
    
    // Находим свободный слот в массиве открытых файлов
    int vtpc_fd = -1;
    for (int i = 0; i < 1024; i++) {
        if (open_files[i] == NULL) {
            vtpc_fd = i;
            break;
        }
    }
    
    if (vtpc_fd == -1) {
        close(sys_fd);
        errno = EMFILE;
        return -1;
    }
    
    // Создаем структуру информации о файле
    FileInfo *file_info = (FileInfo*)malloc(sizeof(FileInfo));
    if (file_info == NULL) {
        close(sys_fd);
        errno = ENOMEM;
        return -1;
    }
    
    file_info->fd = sys_fd;
    file_info->file_size = file_size;
    file_info->position = 0;
    
    open_files[vtpc_fd] = file_info;
    
    return vtpc_fd;
}

// Закрытие файла
int vtpc_close(int fd) {
    if (fd < 0 || fd >= 1024 || open_files[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    pthread_mutex_lock(&cache_manager.lock);
    
    // Записываем все "грязные" блоки этого файла на диск
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->fd == fd && current->dirty) {
            write_block_to_disk(current);
        }
        current = current->next;
    }
    
    // Удаляем все блоки этого файла из кэша
    current = cache_manager.head;
    while (current != NULL) {
        CacheBlock *next = current->next;
        
        if (current->fd == fd) {
            // Удаляем блок из списка
            if (current->prev) {
                current->prev->next = current->next;
            }
            if (current->next) {
                current->next->prev = current->prev;
            }
            
            if (current == cache_manager.head) {
                cache_manager.head = current->next;
            }
            if (current == cache_manager.tail) {
                cache_manager.tail = current->prev;
            }
            
            free(current);
            cache_manager.block_count--;
        }
        
        current = next;
    }
    
    pthread_mutex_unlock(&cache_manager.lock);
    
    // Закрываем системный файловый дескриптор
    FileInfo *file_info = open_files[fd];
    int result = close(file_info->fd);
    
    // Освобождаем память и очищаем слот
    free(file_info);
    open_files[fd] = NULL;
    
    return result;
}

// Чтение из файла
ssize_t vtpc_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= 1024 || open_files[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    if (buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (count == 0) {
        return 0;
    }
    
    FileInfo *file_info = open_files[fd];
    
    // Проверяем, не выходим ли за пределы файла
    if (file_info->position >= file_info->file_size) {
        return 0; // EOF
    }
    
    // Ограничиваем количество читаемых байт до конца файла
    size_t bytes_to_read = count;
    if (file_info->position + bytes_to_read > file_info->file_size) {
        bytes_to_read = file_info->file_size - file_info->position;
    }
    
    size_t total_bytes_read = 0;
    char *buffer = (char*)buf;
    
    while (total_bytes_read < bytes_to_read) {
        off_t current_pos = file_info->position + total_bytes_read;
        off_t block_num = get_block_number(current_pos);
        off_t block_offset = get_block_offset(current_pos);
        
        // Вычисляем, сколько байт можно прочитать из текущего блока
        size_t bytes_in_block = BLOCK_SIZE - block_offset;
        size_t bytes_needed = bytes_to_read - total_bytes_read;
        size_t bytes_to_copy = (bytes_in_block < bytes_needed) ? bytes_in_block : bytes_needed;
        
        // Ищем блок в кэше
        CacheBlock *block = find_block_in_cache(fd, block_num);
        
        // Если блока нет в кэше, загружаем его
        if (block == NULL) {
            block = allocate_new_block(fd, block_num);
            if (block == NULL) {
                // Если не удалось выделить блок, возвращаем сколько прочитали
                file_info->position += total_bytes_read;
                return total_bytes_read;
            }
        }
        
        // Копируем данные из блока в буфер
        memcpy(buffer + total_bytes_read, 
               block->data + block_offset, 
               bytes_to_copy);
        
        total_bytes_read += bytes_to_copy;
    }
    
    file_info->position += total_bytes_read;
    return total_bytes_read;
}

// Запись в файл
ssize_t vtpc_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= 1024 || open_files[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    if (buf == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (count == 0) {
        return 0;
    }
    
    FileInfo *file_info = open_files[fd];
    size_t total_bytes_written = 0;
    const char *buffer = (const char*)buf;
    
    while (total_bytes_written < count) {
        off_t current_pos = file_info->position + total_bytes_written;
        off_t block_num = get_block_number(current_pos);
        off_t block_offset = get_block_offset(current_pos);
        
        // Вычисляем, сколько байт можно записать в текущий блок
        size_t bytes_in_block = BLOCK_SIZE - block_offset;
        size_t bytes_needed = count - total_bytes_written;
        size_t bytes_to_copy = (bytes_in_block < bytes_needed) ? bytes_in_block : bytes_needed;
        
        // Ищем блок в кэше
        CacheBlock *block = find_block_in_cache(fd, block_num);
        
        // Если блока нет в кэше, создаем новый
        if (block == NULL) {
            block = allocate_new_block(fd, block_num);
            if (block == NULL) {
                file_info->position += total_bytes_written;
                return total_bytes_written;
            }
        }
        
        // Копируем данные из буфера в блок
        memcpy(block->data + block_offset, 
               buffer + total_bytes_written, 
               bytes_to_copy);
        
        // Помечаем блок как "грязный"
        block->dirty = 1;
        block->last_access = time(NULL);
        move_to_front(block);
        
        total_bytes_written += bytes_to_copy;
        
        // Обновляем размер файла, если нужно
        off_t new_end_pos = current_pos + bytes_to_copy;
        if (new_end_pos > file_info->file_size) {
            file_info->file_size = new_end_pos;
        }
    }
    
    file_info->position += total_bytes_written;
    return total_bytes_written;
}

// Перемещение позиции в файле
off_t vtpc_lseek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= 1024 || open_files[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    FileInfo *file_info = open_files[fd];
    off_t new_position;
    
    switch (whence) {
        case SEEK_SET:
            new_position = offset;
            break;
            
        case SEEK_CUR:
            new_position = file_info->position + offset;
            break;
            
        case SEEK_END:
            new_position = file_info->file_size + offset;
            break;
            
        default:
            errno = EINVAL;
            return -1;
    }
    
    // Проверяем, что позиция не отрицательна
    if (new_position < 0) {
        errno = EINVAL;
        return -1;
    }
    
    file_info->position = new_position;
    return new_position;
}

// Синхронизация данных с диском
int vtpc_fsync(int fd) {
    if (fd < 0 || fd >= 1024 || open_files[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    pthread_mutex_lock(&cache_manager.lock);
    
    // Записываем все "грязные" блоки этого файла на диск
    CacheBlock *current = cache_manager.head;
    while (current != NULL) {
        if (current->fd == fd && current->dirty) {
            write_block_to_disk(current);
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&cache_manager.lock);
    
    // Вызываем системный fsync для гарантированной записи на диск
    FileInfo *file_info = open_files[fd];
    int result = fsync(file_info->fd);
    
    return result;
}

// Функция очистки (можно вызвать при завершении программы)
void vtpc_cleanup(void) {
    cleanup_cache();
    
    // Закрываем все открытые файлы
    for (int i = 0; i < 1024; i++) {
        if (open_files[i] != NULL) {
            vtpc_close(i);
        }
    }
}