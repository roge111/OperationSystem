# Базовая лабораторная работа 2

----
Выполнил: Батаргин Егор Александрович
ИСУ:335189
Группа: P3330
---

# Задание
Реализация блочного кэша в пространстве пользователя в виде динамической библиотеки. Операции чтения и записи на диск. Такой кэш позволяет избежать высоких задержек при повторном доступе к данным, так как операция будет выполнена с данными в RAM, а не на диске.

В данной лабораторной работе необходимо реализовать блочный кэш в пространстве пользователя в виде динамической библиотеки. Политику вытеснения страниц и другие элементы задания необходимо получить у преподавателя.

При выполнении работы необходимо реализовать простой API для работы с файлами, предоставляющий пользователю следующие возможности (по аналогии с системным API):

- Открытие файла по заданному пути файла, доступного для чтения. Процедура возвращает некоторый хэндл на файл. Пример: int vtpc_open(const char *path, int flags, int mode).

- Закрытие файла по хэндлу. Пример: int vtpc_close(int fd).

- Чтение данных из файла. Пример: ssize_t vtpc_read(int fd, void buf[.count], size_t count).

- Запись данных в файл. Пример: ssize_t vtpc_write(int fd, const void buf[.count], size_t count).

- Перестановка позиции указателя на данные файла. Достаточно поддержать только абсолютные координаты. Пример: ​​​​​​​off_t vtpc_lseek(int fd, off_t offset, int whence).

- Синхронизация данных из кэша с диском. Пример: int vtpc_fsync(int fd).

- Операции с диском разработанного блочного кеша должны производиться в обход страничного кеша операционной системы.

В рамках проверки работоспособности разработанного блочного кэша необходимо адаптировать указанную преподавателем программу-загрузчик из ЛР 1, добавив использование кэша. Запустите программу и убедитесь, что она корректно работает. Сравните производительность до и после.

Точкой входа в ваш модуль являются файлы vtpc.h и vtpc.c. Именно этот API вызывается тестовым фреймворком, сравнивающим поведения вашего vtpc и libc.

# Ограничения
Программа (комплекс программ) должна быть реализован на языке C.

Если по выданному варианту задана политика вытеснения Optimal, то необходимо предоставить пользователю возможность подсказать page cache, когда будет совершен следующий доступ к данным. Это можно сделать либо добавив параметр в процедуры read и write (например, ssize_t vtpc_read(int fd, void buf[.count], size_t count, access_hint_t hint)), либо добавив еще одну функцию в API (например, int vtpc_advice(int fd, off_t offset, access_hint_t hint)). access_hint_t в данном случае – это абсолютное время или временной интервал, по которому разработанное API будет определять время последующего доступа к данным.

Запрещено использовать высокоуровневые абстракции над системными вызовами.

# Выполнение работы

Основным файлом для работы считается vtpc.c, в котором реализуется API для работы с файлами.

Среди библиотек данного файла можно найти `stdio.h` - скажу сразу, использовалась для отладочного вывода.

# vtpc.c

По сути мы сделали свою абстракцию для системных вызовов.

Начнем по порядку

```

// Размер блока (обычно соответствует размеру страницы или сектору диска)
#define BLOCK_SIZE 4096


// Максимальное количество блоков в кэше
#define MAX_CACHE_BLOCKS 1024

```
Размер блока 4096 выбран не случайно, так как он соответствует размеру страницы в операционных системах.

Размер взят произвольный. Для кеша составит 1024 * 4 = 4 МБ кеша.


Дальше мы создаем структуру для хранения информации о блоке кеша.
```
typedef struct CacheBlock {
    int fd;                 // Файловый дескриптор
    off_t block_number;     // Номер блока в файле
    char data[BLOCK_SIZE];  // Данные блока
    int dirty;              // Флаг "грязного" блока (изменён, но не записан)
    time_t last_access;     // Время последнего доступа (для LRU)
    struct CacheBlock *prev; ///< Указатель на предыдущий блок в списке LRU
    struct CacheBlock *next; ///< Указатель на следующий блок в списке LRU
} CacheBlock;
```

Как видно в комментариях, мы храним то, в каком файловом дескрипторе находится блок, номер блока в файле, данные блока, флаг "грязного" блока (изменен, но не записан), время последнего доступа (для LRU), указатели на предыдущий и следующий блоки в списке LRU.

Что такое дескриптор? Это идентификатор файла, по которому потом можно обратиться. Указывает на то, какой файл у нас в этом блоке.



Дальше мы создаем структуру для хранения информации о файле
```
typedef struct FileInfo {
    int fd;                 // Системный файловый дескриптор
    off_t file_size;        // Размер файла
    off_t position;         // Текущая позиция в файле
} FileInfo;

```

Я думаю, по комментариям все понятно.


```
typedef struct CacheManager {
    CacheBlock *head;       // Начало списка LRU (самый недавно использованный)
    CacheBlock *tail;       // Конец списка LRU (самый давно использованный)
    int block_count;        // Текущее количество блоков в кэше
    volatile int lock;   // Спинлок для потокобезопасности
} CacheManager;
```
Это структура для управления кешем. Тут мы будем содержать и обновлять информацию о том, какой файл был недавно использованным, а какой - давно. Это нам необходимо для реализации LRU. LRU - это алгоритм вытеснения данных. При заполненном кеше мы вытесняем из него самые давно использованные данные.
Для начала нам надо установить начальные значения для всех указталей и счетчиков.

```
static void init_cache(void) {
    cache_manager.head = NULL;
    cache_manager.tail = NULL;
    cache_manager.block_count = 0;
    spinlock_init(&cache_manager.lock);
    
    // Инициализируем массив открытых файлов
    for (int i = 0; i < 1024; i++) {
        open_files[i] = NULL;
    }
}
```
Дальше реализуем наш очиститель кеша.

```
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
```
Эта функция выполняет полную очистку кэша, записывая все "грязные" блоки на диск и освобождая память.

Процесс очистки кэша:
1. Блокируем кэш для потокобезопасности:
   ```
   spin_lock(&cache_manager.lock);
   ```

2. Записываем все "грязные" блоки на диск и освобождаем память:
   ```
   CacheBlock *current = cache_manager.head;
   while (current != NULL) {
       if (current->dirty) {
           write_block_to_disk(current);
       }
       CacheBlock *next = current->next;
       raw_munmap(current, sizeof(CacheBlock));
       current = next;
   }
   ```

3. Сбрасываем параметры кэша:
   ```
   cache_manager.head = NULL;
   cache_manager.tail = NULL;
   cache_manager.block_count = 0;
   ```

4. Разблокируем кэш:
   ```
   spin_unlock(&cache_manager.lock);
   ```

5. Закрываем все открытые файлы и освобождаем память:
   ```
   for (int i = 0; i < 1024; i++) {
       if (open_files[i] != NULL) {
           FileInfo *info = open_files[i];
           raw_close(info->fd);
           raw_munmap(info, sizeof(FileInfo));
           open_files[i] = NULL;
       }
   }
   ```

Теперь кратко об функциях блокировки кеша и разблокировки кеша.

Дабы избежать использования высокоуровневых абстракций над системными вызовами, мы сделаем свою абстракцию над ними.

```
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
```

В цикле `while` в функции `spin_lock` мы проверяем, заблокирован ли кеш. Если заблокирован, то мы выполняем паузу и проверяем снова. Если кеш не заблокирован, то мы заблокируем его и выходим из цикла. Такой механизм защищает от гонки данных. Важно отметить, что `__sync_lock_test_and_set` - атомарная операция, которая обрабатывается напрямую компилятором. Это вложено в GCC и не является высокоуровневой абстракцией.


Поехали дальше

Реализуем функцию для поиска блока в кеше

```
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
```

На вход программа принимает файловый дескриптор и номер блока. Функция ищет блок в кеше и если он есть, то обновляет время доступа и перемещает блок в начало списка. Если блок не найден, то возвращает NULL.

Про функцию `move_to_front` как раз сейчас поговорим.
```
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
```
А теперь по тому, как оно работает

Сначала выполним проверку, что блок не в начале списка.

Проверим, есть ли у блока предыдущий блок. 

Если есть, то ставим у предыдущего блока следующим тот блок, что следующий от текущего. И то же самое касается предыдущего.

ДЛЯ СЕБЯ
---

`block->prev->next = block->next;` - игра указателей.
Представим, что есть блок A, B и C
```
A -> B -> C
```
У блока B `prev` указывает на блок A, а `next` на блок C.

block->prev - указатель на предыдущий блок A
block->prev->next - указатель на следующий блок относительно блока A. В который мы сохраняем block->next = C (относительно текущего блока B)

```
block->prev - A
block->next - C
block->prev->next = block->next
Теперь у блока A next указывает на блок C, а блока C prev указывает на блок A
```

----

Дальше идем

Если блок был хвостом, то новый хвост - это будет предыдущий блок относительно изымаемого (текущего)

Обновляем данные блока:
- удаляем у блока предыдущий (он же голова)
- Теперь его следующий блок - это прошлый головной блок.
- У того блока, что еще в голове делаем предыдущим блок тот, что вставляем в начало
- обновления `head` на новый блок

Ну и если список был пуст, блок становится и хвостом еще

Сделаем простую функцию, что будет вычислять номер блока по его позиции:
```
static off_t get_block_number(off_t position) {
    return position / BLOCK_SIZE;
}
```
Тут все просто.

А смещение внутри блока - это остаток от деления позиции на размер блока.
```
static off_t get_block_offset(off_t position) {
    return position % BLOCK_SIZE;
}
```

Реализуем функцию чтения блока из диска физического:
```
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
```
Про чтение
---
На вход принимает блок.

```
FileInfo *file_info = open_files[block->fd];
    if (!file_info) return;
```

Дальше вычисление позиции на диске
```
off_t pos = block->block_number * BLOCK_SIZE;
```

Сохраняем текущую позицию в файле, чтобы потом вернуться
```
off_t old_pos = raw_lseek(file_info->fd, 0, SEEK_CUR);
```

Перемещаемся в нужную позицию для чтения блока
```
if (old_pos < 0 || raw_lseek(file_info->fd, pos, SEEK_SET) < 0) {
    if (old_pos >= 0) raw_lseek(file_info->fd, old_pos, SEEK_SET);
    raw_memset(block->data, 0, BLOCK_SIZE);
    return;
}
```

Читаем данные блока
```
ssize_t bytes = raw_read(file_info->fd, block->data, BLOCK_SIZE);
```

Возвращаемся в исходную позицию
```
raw_lseek(file_info->fd, old_pos, SEEK_SET);
```

Если прочитали меньше, чем размер блока, заполняем остаток нулями
```
if (bytes < BLOCK_SIZE) {
    if (bytes > 0) {
        raw_memset(block->data + bytes, 0, BLOCK_SIZE - bytes);
    } else {
        raw_memset(block->data, 0, BLOCK_SIZE);
    }
}
```

Сбрасываем флаг "грязного" блока и обновляем время последнего доступа

```
block->dirty = 0;
block->last_access = raw_time();
```


Реализуем функцию записи блока на диск:
```
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
```
Про запись
---
На вход принимает блок.

```
FileInfo *file_info = open_files[block->fd];
    if (!file_info || !block->dirty) return;
```
Проверяем, что файл открыт и блок "грязный" (требует записи на диск).

Вычисляем позицию на диске и сохраняем текущую позицию в файле:
```
off_t pos = block->block_number * BLOCK_SIZE;
off_t old_pos = raw_lseek(file_info->fd, 0, SEEK_CUR);
```

Перемещаемся в нужную позицию для записи блока:
```
if (old_pos < 0 || raw_lseek(file_info->fd, pos, SEEK_SET) < 0) {
    if (old_pos >= 0) raw_lseek(file_info->fd, old_pos, SEEK_SET);
    return;
}
```

Записываем данные блока на диск:
```
ssize_t written = raw_write(file_info->fd, block->data, BLOCK_SIZE);
```

Возвращаемся в исходную позицию:
```
raw_lseek(file_info->fd, old_pos, SEEK_SET);
```

Если запись прошла успешно, сбрасываем флаг "грязного" блока:
```
if (written == BLOCK_SIZE) {
    block->dirty = 0;
}
```

Реализуем функцию вытеснения блока из кэша:
```
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
```
Про вытеснение
---
Эта функция реализует алгоритм LRU (Least Recently Used) для вытеснения блоков из кэша.

Проверяем, что в кэше есть блоки:
```
if (cache_manager.tail == NULL) {
    return;
}
```

Выбираем блок для вытеснения (самый давно использованный - в хвосте списка):
```
CacheBlock *to_evict = cache_manager.tail;
```

Если блок "грязный", записываем его на диск перед вытеснением:
```
if (to_evict->dirty) {
    write_block_to_disk(to_evict);
}
```

Удаляем блок из списка LRU:
```
if (to_evict->prev) {
    to_evict->prev->next = NULL;
}

cache_manager.tail = to_evict->prev;

if (cache_manager.head == to_evict) {
    cache_manager.head = NULL;
}
```

Освобождаем память, занятую блоком:
```
raw_munmap(to_evict, sizeof(CacheBlock));
cache_manager.block_count--;
```

Реализуем функцию выделения нового блока:
```
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
```
Про выделение
---
Эта функция выделяет новый блок в кэше, при необходимости вытесняя старые блоки.

Блокируем кэш для потокобезопасности:
```
spin_lock(&cache_manager.lock);
```

Проверяем, не превышен ли лимит блоков в кэше:
```
if (cache_manager.block_count >= MAX_CACHE_BLOCKS) {
    evict_lru_block();
}
```

Создаем новый блок в памяти:
```
CacheBlock *new_block = raw_mmap(sizeof(CacheBlock));
if (!new_block) {
    spin_unlock(&cache_manager.lock);
    return NULL;
}
```

Инициализируем поля блока:
```
raw_memset(new_block, 0, sizeof(CacheBlock));
new_block->fd = fd;
new_block->block_number = block_number;
new_block->last_access = raw_time();
```

Читаем данные блока с диска:
```
read_block_from_disk(new_block);
```

Добавляем блок в начало списка LRU:
```
new_block->next = cache_manager.head;
if (cache_manager.head) {
    cache_manager.head->prev = new_block;
}

cache_manager.head = new_block;

if (cache_manager.tail == NULL) {
    cache_manager.tail = new_block;
}
```

Увеличиваем счетчик блоков и разблокируем кэш:
```
cache_manager.block_count++;

spin_unlock(&cache_manager.lock);
return new_block;
```

API функции
---

`int vtpc_open(const char *path, int flags, int mode)` - открывает файл и возвращает дескриптор:

Эта функция открывает файл и возвращает виртуальный дескриптор файла для использования с другими функциями API.


Процесс открытия файла:
1. Проверяем, была ли инициализирована система кэширования:
```
static int initialized = 0;
if (!initialized) {
    raw_memset(&cache_manager, 0, sizeof(cache_manager));
    raw_memset(open_files, 0, sizeof(open_files));
    initialized = 1;
}
```

2. Преобразуем флаги VTPC в системные флаги и проверяем наличие O_DIRECT:
```
int sys_flags = 0;
int use_direct_io = 0;

// Копируем флаги как есть
sys_flags = flags;

// Проверяем, передал ли  O_DIRECT
if (flags & 00040000) { // O_DIRECT
    use_direct_io = 1;
} else {
    // Если  НЕ передает O_DIRECT, НЕ добавляем его
    use_direct_io = 0;
    sys_flags &= ~O_DIRECT; // Убеждаемся, что O_DIRECT не установлен
}
```

3. Открываем файл с преобразованными флагами:
```
int actual_mode = mode;
if (actual_mode == 0)
{
    actual_mode = 0666;
}

// Открываем файл
int sys_fd = raw_open(path, sys_flags, actual_mode);

// Если открываем с O_DIRECT и не получилось, пробуем без него
if (sys_fd < 0 && (sys_flags & O_DIRECT))
{
    sys_flags &= ~O_DIRECT;
    sys_fd = raw_open(path, sys_flags, actual_mode);
    if (sys_fd < 0)
        return -1;
    use_direct_io = 0;
}
else if (sys_fd < 0)
{
    return -1;
}
```

4. Получаем размер файла:
```
off_t size = raw_lseek(sys_fd, 0, SEEK_END);
if (size < 0 || raw_lseek(sys_fd, 0, SEEK_SET) < 0)
{
    raw_close(sys_fd);
    return -1;
}
```

5. Ищем свободный слот для FileInfo:
```
int vtpc_fd = -1;
for (int i = 0; i < 1024; i++)
{
    if (!open_files[i])
    {
        vtpc_fd = i;
        break;
    }
}
```

6. Выделяем память для FileInfo и инициализируем его:
```
FileInfo *info = raw_mmap_aligned(sizeof(FileInfo));
if (!info)
{
    raw_close(sys_fd);
    return -1;
}

info->fd = sys_fd;
info->file_size = size;
info->position = 0;
info->use_direct_io = use_direct_io; // Сохраняем режим

open_files[vtpc_fd] = info;
return vtpc_fd;
```

int vtpc_close(int fd) - закрывает файл по дескриптору:

Эта функция закрывает файл и освобождает все связанные с ним ресурсы.

Процесс закрытия файла:
1. Проверяем корректность дескриптора:
   ```
   if (fd < 0 || fd >= 1024 || !open_files[fd]) return -1;
   ```

2. Блокируем кэш для потокобезопасности:
   ```
   spin_lock(&cache_manager.lock);
   ```

3. Записываем все "грязные" блоки этого файла на диск:
   ```
   CacheBlock *curr = cache_manager.head;
   while (curr) {
       if (curr->fd == fd && curr->dirty) {
           write_block_to_disk(curr);
       }
       curr = curr->next;
   }
   ```

4. Удаляем блоки этого файла из кэша:
   ```
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
   ```

5. Разблокируем кэш:
   ```
   spin_unlock(&cache_manager.lock);
   ```

6. Закрываем файл и освобождаем память:
   ```
   FileInfo *info = open_files[fd];
   raw_close(info->fd);
   raw_munmap(info, sizeof(FileInfo));
   open_files[fd] = 0;
   ```

`ssize_t vtpc_read(int fd, void *buf, size_t count)` - читает данные из файла:

Эта функция читает указанное количество байт из файла в буфер.

Процесс чтения данных:
1. Проверяем корректность параметров:
```
if (fd < 0 || fd >= 1024 || !open_files[fd] || !buf) return -1;
```

2. Определяем, сколько байт нужно прочитать:
```
FileInfo *info = open_files[fd];
if (info->position >= info->file_size) return 0;

size_t to_read = count;
if (info->position + (off_t)to_read > info->file_size) {
    to_read = info->file_size - info->position;
}
```

3. Читаем данные блоками:
```
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
```

4. Обновляем позицию в файле:
```
info->position += (off_t)total;
return total;
   ```

ssize_t vtpc_write(int fd, const void *buf, size_t count) - записывает данные в файл:

Эта функция записывает указанное количество байт из буфера в файл.

Процесс записи данных:
1. Проверяем корректность параметров:
   ```
   if (fd < 0 || fd >= 1024 || !open_files[fd] || !buf) return -1;
   ```

2. Записываем данные блоками:
   ```
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
       // ... (код перемещения блока)
       
       total_bytes_written += bytes_to_copy;
       
       // Обновляем размер файла, если нужно
       off_t new_end_pos = current_pos + (off_t)bytes_to_copy;
       if (new_end_pos > info->file_size) {
           info->file_size = new_end_pos;
       }
   }
   ```

3. Обновляем позицию в файле:
   ```
   info->position += (off_t)total_bytes_written;
   return total_bytes_written;
   ```

off_t vtpc_lseek(int fd, off_t offset, int whence) - перемещает указатель позиции в файле:

Эта функция устанавливает позицию указателя в файле.

Поддерживаемые значения whence:
- SEEK_SET: позиция устанавливается относительно начала файла
- SEEK_CUR: позиция устанавливается относительно текущей позиции
- SEEK_END: позиция устанавливается относительно конца файла

Процесс перемещения указателя:
1. Проверяем корректность дескриптора:
```
if (fd < 0 || fd >= 1024 || !open_files[fd]) return -1;
```

2. Вычисляем новую позицию в зависимости от whence:
```
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
```

3. Проверяем, что позиция не отрицательна:
```
if (new_position < 0) {
    return -1;
}
```

4. Устанавливаем новую позицию:
```
info->position = new_position;
return new_position;
```

int vtpc_fsync(int fd) - синхронизирует данные файла с диском:

Эта функция записывает все "грязные" блоки файла на диск и вызывает системный fsync.

Процесс синхронизации:
1. Проверяем корректность дескриптора:
```
if (fd < 0 || fd >= 1024 || !open_files[fd]) return -1;
```

2. Блокируем кэш:
```
spin_lock(&cache_manager.lock);
```

3. Записываем все "грязные" блоки этого файла на диск:
```
CacheBlock *current = cache_manager.head;
while (current != NULL) {
    if (current->fd == fd && current->dirty) {
        write_block_to_disk(current);
    }
    current = current->next;
}
```

4. Разблокируем кэш:
```
spin_unlock(&cache_manager.lock);
```

5. Вызываем системный fsync:
```
FileInfo *info = open_files[fd];
int result = raw_fsync(info->fd);

return result;
```

# Новые функции в библиотеке VTPC

## Функции для работы с прямым доступом к диску

### vtpc_open с флагами
Функция `int vtpc_open(const char *path, int flags, int mode)` открывает файл с дополнительными флагами:
- `VTPC_O_RDONLY` - открытие только для чтения
- `VTPC_O_WRONLY` - открытие только для записи
- `VTPC_O_RDWR` - открытие для чтения и записи
- `VTPC_O_DIRECT` - прямой доступ к диску (обход кэша ОС)
- `VTPC_O_SYNC` - синхронная запись

Прямой доступ к диску позволяет обходить кэш операционной системы, что может быть полезно для специфических задач, где требуется гарантированная запись данных на диск без использования промежуточного кэширования.

### vtpc_read_aligned и vtpc_write_aligned
Функции `ssize_t vtpc_read_aligned(int fd, void *aligned_buf, size_t count)` и `ssize_t vtpc_write_aligned(int fd, const void *aligned_buf, size_t count)` работают с выровненными буферами памяти для прямого доступа к диску.

Выровненные буферы - это буферы памяти, адрес которых кратен определенному значению (в нашем случае 512 байт). Это требование для работы с прямым доступом к диску.

### vtpc_read_direct и vtpc_write_direct
Функции `ssize_t vtpc_read_direct(int fd, void *aligned_buf, size_t count)` и `ssize_t vtpc_write_direct(int fd, const void *aligned_buf, size_t count)` обеспечивают принудительное чтение/запись напрямую с диска, обходя кэш, даже если файл открыт без флага O_DIRECT.

### vtpc_alloc_aligned_buffer и vtpc_free_aligned_buffer
Функции `void* vtpc_alloc_aligned_buffer(size_t size)` и `void vtpc_free_aligned_buffer(void *buffer)` позволяют выделять и освобождать выровненную память для работы с прямым доступом к диску.

## Как работает прямой доступ к диску

Прямой доступ к диску (O_DIRECT) позволяет обходить кэш операционной системы при чтении и записи данных. Это может быть полезно в следующих случаях:

1. **Гарантированная запись данных** - данные сразу записываются на диск, минуя кэш операционной системы.
2. **Специфические требования к производительности** - в некоторых случаях прямой доступ может быть быстрее, особенно при больших объемах данных.
3. **Тестирование производительности диска** - позволяет измерить реальную производительность диска без влияния кэша.

Однако, прямой доступ имеет и недостатки:
1. **Требования к выравниванию** - буферы памяти и размеры операций должны быть выровнены по определенным границам.
2. **Потенциально меньшая производительность** - кэш операционной системы часто улучшает производительность за счет уменьшения количества операций ввода-вывода.

## Функции мониторинга производительности

### vtpc_get_cache_size и vtpc_get_dirty_count
Функции `size_t vtpc_get_cache_size(void)` и `size_t vtpc_get_dirty_count(void)` позволяют получить информацию о текущем состоянии кэша:
- `vtpc_get_cache_size()` возвращает количество блоков в кэше
- `vtpc_get_dirty_count()` возвращает количество "грязных" блоков (требующих записи на диск)

### vtpc_print_cache_stats
Функция `void vtpc_print_cache_stats(void)` выводит статистику кэша на экран.

## Функция очистки ресурсов

### vtpc_cleanup
Функция `void vtpc_cleanup(void)` записывает все "грязные" блоки на диск и освобождает все ресурсы, связанные с файловой системой. Эта функция должна вызываться при завершении работы с библиотекой.

# Сборка проекта

Для сборки проекта используется Makefile со следующими целями:

- `make all` - сборка всех компонентов
- `make libvtpc.so` - сборка динамической библиотеки VTPC
- `make memory_loader_vtpc` - сборка тестовой программы MemoryLoader
- `make test` - запуск тестов
- `make clean` - очистка проекта

# Использование библиотеки

Для использования библиотеки в своих программах необходимо:
1. Подключить заголовочный файл `vtpc.h`
2. Скомпилировать программу с использованием библиотеки libvtpc.so
3. При запуске программы убедиться, что библиотека доступна в системе

Пример использования:
```c
#include "vtpc.h"

int main() {
    // Открытие файла с прямым доступом
    int fd = vtpc_open("test.txt", VTPC_O_RDWR | VTPC_O_DIRECT);
    
    // Выделение выровненного буфера
    char* buffer = vtpc_alloc_aligned_buffer(4096);
    
    // Чтение данных
    vtpc_read_direct(fd, buffer, 4096);
    
    // Запись данных
    vtpc_write_direct(fd, buffer, 4096);
    
    // Закрытие файла
    vtpc_close(fd);
    
    // Освобождение буфера
    vtpc_free_aligned_buffer(buffer);
    
    // Очистка ресурсов
    vtpc_cleanup();
    
    return 0;
}
```
# Анализ

Для анализа я взял программу из БЛР1. Сейчас это `MemoryLoader_vtpc.c` . В ней реалилваны две функции запуска: 
- `randomReadTest_vtpc` -это запуск нагрузки с использование нашего кеша
- `randomReadTest_system` - это запуск с систмеными вызовами, но в обход кеша. 

Мы должны сравнить время работы. 

### Гипотиза

Вызов через системные операци идет без участие кеша. Учитывая, что файл, который мы считывая, очень большой, то операция чтения и записи на диск  будет очень не выгодной. 

Когда мы читаем через vtpc - мы читаем сохраняем файлы в наш созданные кеш, где идет политика вытиснения LRU. И не смотря на то, что мы читаем рандомные позиции, то всегда есть шанс, что программа попадает в ту же страницу, что уже в кеше. В таком случае программа не загружает из диска, а грузит уже из кеша. Что ускоряет время чтения и записи

LRU нас будет спасать по той причине, что его политика вытиснения вытисняет те блоки, которые у нас давно не используются. Соответсвенно при частых попадания в один и тот же блок время доступа к нему сокращается, и нам не надо его снова читать с диска. Если же гооврить про тот же FIFO - то его политика в том, что мы удаляем тот блок, который был раньше добавлен в кеш. Но при этом может удалится тот блок, который нам часто нужен. 

При рандомном чтении попадание в один и тот же блок может быть, поэтому время работы ускоряется.  


Почему же при O_DIRECT медленее ?

 - Обход системного кеша: при использовании O_DIRECT, данные не будут записаны в системный кеш, а будут напрямую записаны на диск. Таким образом, время записи данных на диск будет больше, чем время записи данных в системный кеш.
 - Синхронность: O_DIRECT часто работает синхронно. То есть он будет ждать, когда изменения запуштся на диск 

 Оба этих пункта говорят о том, что программа будет работать медленнее с O_DIRECT, чем с кешем LRU.


То есть главная гипотиза:
```
Программы с vtpc будет работать быстрее, чем с системными вызовами.
```

### Результат



```
=== Memory Loader с VTPC Cache ===
=== Сравнение производительности VTPC и системных вызовов ===

Запуск теста с VTPC...

--- Детали операций VTPC ---
Итерация | Индекс (offset) | Попаданий в итерации | Всего попаданий
---------|-----------------|----------------------|----------------
        1 |       713129984 |                   40 | 40
        2 |       713129984 |                   40 | 80
        3 |       713129984 |                   40 | 120
        4 |       119484416 |                   40 | 160
        5 |       119484416 |                   40 | 200
        6 |       119484416 |                   40 | 240
        7 |       119484416 |                   40 | 280
        8 |       119484416 |                   40 | 320
        9 |       119484416 |                   40 | 360
       10 |       119484416 |                   40 | 400
       11 |       119484416 |                   40 | 440
       12 |       119484416 |                   40 | 480
       13 |       119484416 |                   40 | 520
       14 |       119484416 |                   40 | 560
       15 |       119484416 |                   40 | 600
       16 |       119484416 |                   40 | 640
       17 |       119484416 |                   40 | 680
       18 |       799358976 |                   40 | 720
       19 |       799358976 |                   40 | 760
       20 |       799358976 |                   40 | 800
       21 |       799358976 |                   40 | 840
       22 |       799358976 |                   40 | 880
       23 |       799358976 |                   40 | 920
       24 |       799358976 |                   40 | 960
       25 |       799358976 |                   40 | 1000
       26 |       799358976 |                   40 | 1040
       27 |       799358976 |                   40 | 1080
       28 |       799358976 |                   40 | 1120
       29 |       799358976 |                   40 | 1160
       30 |       799358976 |                   40 | 1200
       31 |       799358976 |                   40 | 1240
       32 |       787636224 |                   40 | 1280
       33 |       787636224 |                   40 | 1320
       34 |       787636224 |                   40 | 1360
       35 |       787636224 |                   40 | 1400
       36 |       787636224 |                   40 | 1440
       37 |       787636224 |                   40 | 1480
       38 |       787636224 |                   40 | 1520
       39 |       787636224 |                   40 | 1560
       40 |       787636224 |                   40 | 1600
       41 |       787636224 |                   40 | 1640
       42 |       787636224 |                   40 | 1680
       43 |       787636224 |                   40 | 1720
       44 |       787636224 |                   40 | 1760
       45 |       787636224 |                   40 | 1800
       46 |       787636224 |                   40 | 1840
       47 |       425160704 |                   40 | 1880
       48 |       425160704 |                   40 | 1920
       49 |       425160704 |                   40 | 1960
       50 |       425160704 |                   40 | 2000
       51 |       425160704 |                   40 | 2040
       52 |       425160704 |                   40 | 2080
       53 |       425160704 |                   40 | 2120
       54 |       425160704 |                   40 | 2160
       55 |       425160704 |                   40 | 2200
       56 |       425160704 |                   40 | 2240
       57 |       425160704 |                   40 | 2280
       58 |       425160704 |                   40 | 2320
       59 |        96092160 |                   40 | 2360
       60 |        96092160 |                   40 | 2400
       61 |        96092160 |                   40 | 2440
       62 |        96092160 |                   40 | 2480
       63 |        96092160 |                   40 | 2520
       64 |        96092160 |                   40 | 2560
       65 |        96092160 |                   40 | 2600
       66 |        96092160 |                   40 | 2640
       67 |        96092160 |                   40 | 2680
       68 |        96092160 |                   40 | 2720
       69 |        96092160 |                   40 | 2760
       70 |        96092160 |                   40 | 2800
       71 |        96092160 |                   40 | 2840
       72 |       493150208 |                   40 | 2880
       73 |       493150208 |                   40 | 2920
       74 |       493150208 |                   40 | 2960
       75 |       493150208 |                   40 | 3000
       76 |       493150208 |                   40 | 3040
       77 |       493150208 |                   40 | 3080
       78 |       493150208 |                   40 | 3120
       79 |       493150208 |                   40 | 3160
       80 |       493150208 |                   40 | 3200
       81 |       493150208 |                   40 | 3240
       82 |       493150208 |                   40 | 3280
       83 |       493150208 |                   40 | 3320
       84 |       493150208 |                   40 | 3360
       85 |       269070336 |                   40 | 3400
       86 |       269070336 |                   40 | 3440
       87 |       269070336 |                   40 | 3480
       88 |       269070336 |                   40 | 3520
       89 |       269070336 |                   40 | 3560
       90 |       269070336 |                   40 | 3600
       91 |       269070336 |                   40 | 3640
       92 |       269070336 |                   40 | 3680
       93 |       269070336 |                   40 | 3720
       94 |       269070336 |                   40 | 3760
       95 |       269070336 |                   40 | 3800
       96 |       269070336 |                   40 | 3840
       97 |      1086660608 |                   40 | 3880
       98 |      1086660608 |                   40 | 3920
       99 |      1086660608 |                   40 | 3960
      100 |      1086660608 |                   40 | 4000

VTPC: Found number 0 times
Всего попаданий за тест: 4000

--- Итоговая статистика кэша VTPC ---
Всего попаданий (hits): 4000
Всего промахов (misses): 1100
Процент попаданий: 78.43%

Запуск теста с системными вызовами...

--- Детали операций System ---
Итерация | Индекс (offset) 
---------|-----------------
        1 |      1238088192 
        2 |      1238088192 
        3 |      1238088192 
        4 |      1238088192 
        5 |      1238088192 
        6 |      1238088192 
        7 |      1238088192 
        8 |      1192876544 
        9 |      1192876544 
       10 |      1192876544 
       11 |      1192876544 
       12 |      1192876544 
       13 |      1192876544 
       14 |      1192876544 
       15 |      1192876544 
       16 |      1192876544 
       17 |       209761792 
       18 |       209761792 
       19 |       209761792 
       20 |       209761792 
       21 |       209761792 
       22 |       209761792 
       23 |       209761792 
       24 |       209761792 
       25 |       209761792 
       26 |       209761792 
       27 |       661609472 
       28 |       661609472 
       29 |       661609472 
       30 |       661609472 
       31 |       661609472 
       32 |       661609472 
       33 |       661609472 
       34 |       661609472 
       35 |       661609472 
       36 |       661609472 
       37 |       816472576 
       38 |       816472576 
       39 |       816472576 
       40 |       816472576 
       41 |       816472576 
       42 |       816472576 
       43 |       816472576 
       44 |       816472576 
       45 |       816472576 
       46 |       816472576 
       47 |       739500544 
       48 |       739500544 
       49 |       739500544 
       50 |       739500544 
       51 |       739500544 
       52 |       739500544 
       53 |       739500544 
       54 |       739500544 
       55 |       739500544 
       56 |       739500544 
       57 |       739500544 
       58 |       834281472 
       59 |       834281472 
       60 |       834281472 
       61 |       834281472 
       62 |       834281472 
       63 |       834281472 
       64 |       834281472 
       65 |       834281472 
       66 |       834281472 
       67 |       834281472 
       68 |      1001070080 
       69 |      1001070080 
       70 |      1001070080 
       71 |      1001070080 
       72 |      1001070080 
       73 |      1001070080 
       74 |      1001070080 
       75 |      1001070080 
       76 |      1001070080 
       77 |      1001070080 
       78 |       152453632 
       79 |       152453632 
       80 |       152453632 
       81 |       152453632 
       82 |       152453632 
       83 |       152453632 
       84 |       152453632 
       85 |       152453632 
       86 |       152453632 
       87 |       152453632 
       88 |       575353344 
       89 |       575353344 
       90 |       575353344 
       91 |       575353344 
       92 |       575353344 
       93 |       575353344 
       94 |       575353344 
       95 |       575353344 
       96 |       575353344 
       97 |       575353344 
       98 |       616789504 
       99 |       616789504 
      100 |       616789504 

System: Found number 0 times

=== РЕЗУЛЬТАТЫ СРАВНЕНИЯ ===
VTPC:
  Время выполнения: 7615557 мкс (7.616 сек)
  Загрузка CPU: user=6.3%, system=1.7%, wait=0.2%
  Переключения контекста: 119426
  Процессов: 0
  Cache Hits: 4000
  Cache Misses: 1100

System Calls:
  Время выполнения: 9978968 мкс (9.979 сек)
  Загрузка CPU: user=6.4%, system=1.7%, wait=0.2%
  Переключения контекста: 115659
  Процессов: 0

=== СРАВНЕНИЕ ===
Ускорение VTPC vs System: 1.31x
✅ VTPC работает БЫСТРЕЕ системных вызовов
```
При всех запусках показатель времени работы с системные файлами меньше, чем вермени работы с vtpc. Посмоотрим, почему так6

Первая причина у нас заключается в том, что при рандомном чтении мы сипользуем вот такую строчку:
```
srand((unsigned int)time(NULL) + getpid());
```

Она делает так, что у нас последовательность рандомных значений обновляет раз в секунду, и выходит, что rand в течении секунды выдает одно и то же значение. А значит мы попадаем в кеш. Но даже если убрать это, то согласно cashe hit, мы за одну итерцию (одну генерацию) выполняем несколько раз запрос к однойму и тому же блоку. Каждый раз у нас 40 попаданий.  