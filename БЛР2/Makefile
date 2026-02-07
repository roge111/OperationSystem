# Makefile для MemoryLoader с VTPC
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread -D_GNU_SOURCE
LDFLAGS = -pthread -lm

# Цели
TARGETS = libvtpc.so memory_loader_vtpc test_vtpc

all: $(TARGETS)

# Библиотека VTPC
libvtpc.so: vtpc.c vtpc.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ vtpc.c

# Загрузчик с VTPC кэшем
memory_loader_vtpc: MemoryLoader_vtpc.c libvtpc.so
	$(CC) $(CFLAGS) -o $@ MemoryLoader_vtpc.c -L. -lvtpc -Wl,-rpath,.

# Простой тест VTPC
test_vtpc: test_vtpc.c libvtpc.so
	$(CC) $(CFLAGS) -o $@ test_vtpc.c -L. -lvtpc -Wl,-rpath,.

# Тестирование
test: test_vtpc
	@echo "=== Запуск простого теста VTPC ==="
	./test_vtpc

# Тест загрузчика
test-loader: memory_loader_vtpc
	@echo "=== Запуск Memory Loader с VTPC Cache ==="
	./memory_loader_vtpc

# Очистка
clean:
	rm -f $(TARGETS) *.o *.so memory_loader_vtpc test_vtpc test_vtpc_file.bin

.PHONY: all clean test test-loader


# Минимальный тест
test_basic: test_basic.c libvtpc.so
	$(CC) $(CFLAGS) -o $@ test_basic.c -L. -lvtpc -Wl,-rpath,.

test-basic: test_basic
	@echo "=== Минимальный тест VTPC ==="
	./test_basic