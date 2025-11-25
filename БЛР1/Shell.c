#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <limits.h>

// Вспомогательные функции для встроенных команд
void cmd_cd(char *path);
void cmd_pwd();
void cmd_ls(char *path);
void cmd_echo(char **args, int argc);
void cmd_short_path(char *path);
void cmd_cls();
void cmd_help();

// Выполнение встроенной команды в дочернем процессе
int execute_builtin(char **args, int argc) {
    if (argc == 0) return 0;

    if (strcmp(args[0], "cd") == 0) {
        cmd_cd(argc > 1 ? args[1] : NULL);
        return 1;
    }
    if (strcmp(args[0], "pwd") == 0) {
        cmd_pwd();
        return 1;
    }
    if (strcmp(args[0], "ls") == 0) {
        cmd_ls(argc > 1 ? args[1] : ".");
        return 1;
    }
    if (strcmp(args[0], "echo") == 0) {
        cmd_echo(args, argc);
        return 1;
    }
    if (strcmp(args[0], "short-path") == 0) {
        cmd_short_path(argc > 1 ? args[1] : ".");
        return 1;
    }
    if (strcmp(args[0], "cls") == 0) {
        cmd_cls();
        return 1;
    }
    if (strcmp(args[0], "help") == 0) {
        cmd_help();
        return 1;
    }
    return 0; // Не встроенная команда
}

// Разбиение строки на аргументы
int parse_args(char *line, char **args) {
    int argc = 0;
    char *token = strtok(line, " \t\n");
    while (token != NULL && argc < 63) {
        args[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[argc] = NULL;
    return argc;
}

// Функция для разделения команд по &&
void split_commands(char *cmd, char **commands, int *cmd_count) {
    *cmd_count = 0;
    char *start = cmd;
    char *current = cmd;
    
    while (*current) {
        // Ищем &&
        if (*current == '&' && *(current + 1) == '&') {
            // Нашли разделитель
            if (start != current) {
                // Копируем команду
                int len = current - start;
                commands[*cmd_count] = malloc(len + 1);
                strncpy(commands[*cmd_count], start, len);
                commands[*cmd_count][len] = '\0';
                (*cmd_count)++;
            }
            current += 2; // Пропускаем &&
            start = current;
        } else {
            current++;
        }
    }
    
    // Добавляем последнюю команду
    if (start != current) {
        commands[*cmd_count] = malloc(strlen(start) + 1);
        strcpy(commands[*cmd_count], start);
        (*cmd_count)++;
    }
}

// Запуск одной команды через vfork
int run_single_command(char **args, int argc) {
    if (argc == 0) return 1; // Пустая команда - успех
    
    pid_t pid = vfork();
    if (pid == -1) {
        printf("Error: vfork failed\n");
        return 0;
    }
    
    if (pid == 0) {
        // Дочерний процесс
        if (execute_builtin(args, argc)) {
            // Встроенная команда выполнена
            _exit(0);
        } else {
            // Внешняя команда - запускаем через exec
            execvp(args[0], args);
            // Если дошли сюда - ошибка
            fprintf(stderr, "%s: command not found\n", args[0]);
            _exit(127);
        }
    } else {
        // Родительский процесс
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
}

// Основная логика выполнения команд с поддержкой &&
void execute_command(char *cmd) {
    // Удаляем пробелы в начале/конце
    while (*cmd == ' ') cmd++;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && *end == ' ') *end-- = '\0';
    if (*cmd == '\0') return;

    // Разделяем по &&
    char *commands[64] = {0};
    int cmd_count = 0;
    split_commands(cmd, commands, &cmd_count);

    int should_continue = 1;
    for (int i = 0; i < cmd_count && should_continue; i++) {
        char line[1024];
        strncpy(line, commands[i], sizeof(line)-1);
        line[sizeof(line)-1] = '\0';

        // Убираем пробелы вокруг команды
        char *cmd_start = line;
        while (*cmd_start == ' ') cmd_start++;
        char *cmd_end = cmd_start + strlen(cmd_start) - 1;
        while (cmd_end > cmd_start && *cmd_end == ' ') *cmd_end-- = '\0';

        if (*cmd_start == '\0') continue;

        // Парсим аргументы
        char *args[64] = {0};
        int argc = parse_args(cmd_start, args);

        if (argc == 0) continue;

        // Запускаем команду через vfork
        should_continue = run_single_command(args, argc);
    }
    
    // Освобождаем память
    for (int i = 0; i < cmd_count; i++) {
        free(commands[i]);
    }
}

void cmd_cd(char *path) {
    if (path == NULL) {
        path = getenv("HOME");
        if (path == NULL) {
            printf("cd: HOME not set\n");
            return;
        }
    }
    if (chdir(path) != 0) {
        printf("cd: %s: %s\n", path, strerror(errno));
    }
}

void cmd_pwd() {
    char buf[1024];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        printf("%s\n", buf);
    } else {
        printf("pwd: %s\n", strerror(errno));
    }
}

void cmd_ls(char *path) {
    DIR *d = opendir(path);
    if (!d) {
        printf("ls: %s: %s\n", path, strerror(errno));
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        // Пропускаем скрытые файлы (которые начинаются с .)
        if (ent->d_name[0] != '.') {
            printf("%s\n", ent->d_name);
        }
    }
    closedir(d);
}

void cmd_echo(char **args, int argc) {
    if (argc < 2) {
        printf("\n");
        return;
    }
    
    // Ищем перенаправление
    int redirect_index = -1;
    int append = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(args[i], ">") == 0 && i + 1 < argc) {
            redirect_index = i;
            append = 0;
            break;
        }
        if (strcmp(args[i], ">>") == 0 && i + 1 < argc) {
            redirect_index = i;
            append = 1;
            break;
        }
    }
    
    FILE *output = stdout;
    if (redirect_index != -1) {
        output = fopen(args[redirect_index + 1], append ? "a" : "w");
        if (!output) {
            printf("echo: cannot open %s: %s\n", args[redirect_index + 1], strerror(errno));
            return;
        }
    }
    
    // Выводим текст до перенаправления
    int end_index = (redirect_index == -1) ? argc : redirect_index;
    for (int i = 1; i < end_index; i++) {
        fprintf(output, "%s", args[i]);
        if (i < end_index - 1) {
            fprintf(output, " ");
        }
    }
    fprintf(output, "\n");
    
    if (redirect_index != -1) {
        fclose(output);
    }
}

void cmd_short_path(char *path) {
    char buf[4096];
    if (realpath(path, buf) != NULL) {
        printf("%s\n", buf);
    } else {
        printf("short-path: %s: %s\n", path, strerror(errno));
    }
}

void cmd_cls() {
    printf("\033[H\033[J");
}

void cmd_help() {
    printf("Available commands:\n");
    printf("  cd [dir]     - Change directory\n");
    printf("  pwd          - Print working directory\n");
    printf("  ls [dir]     - List directory contents\n");
    printf("  echo [text]  - Echo text with > and >> support\n");
    printf("  short-path   - Show full path\n");
    printf("  cls          - Clear screen\n");
    printf("  help         - This help\n");
    printf("  exit         - Exit shell\n");
    printf("Use && to chain commands\n");
}

int main() {
    char command[1024];

    printf("Simple Shell for Ubuntu (type 'exit' to quit)\n");
    while (1) {
        printf("$ ");
        if (fgets(command, sizeof(command), stdin) == NULL) break;

        // Удаляем \n
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) break;

        execute_command(command);
    }

    printf("Goodbye!\n");
    return 0;
}