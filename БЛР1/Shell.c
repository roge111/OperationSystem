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

// Структура для хранения информации о перенаправлении
typedef struct {
    char *input_file;
    char *output_file;
    int append;
    int syntax_error;
} redirection_info_t;

// Вспомогательные функции для встроенных команд
void cmd_cd(char *path);
void cmd_pwd();
void cmd_ls(char *path);
void cmd_echo(char **args, int argc, redirection_info_t *redir_info);
void cmd_short_path(char *path);
void cmd_cls();
void cmd_help();

/**
 * @brief Выполнение встроенной команды в дочернем процессе
 *
 * @param args Массив аргументов команды
 * @param argc Количество аргументов
 * @param redir_info Информация о перенаправлении
 * @return int 1 если команда встроенная и была выполнена, 0 если команда не встроенная
 */
int execute_builtin(char **args, int argc, redirection_info_t *redir_info) {
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
        cmd_echo(args, argc, redir_info);
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


/**
 * @brief Разбиение строки на аргументы с учетом перенаправления
 *
 * @param line Входная строка для разбора
 * @param args Массив для хранения аргументов
 * @param redir_info Структура для хранения информации о перенаправлении
 * @return int Количество найденных аргументов
 */
int parse_args_with_redirection(char *line, char **args, redirection_info_t *redir_info) {
    int argc = 0;
    char *token = strtok(line, " \t\n");
    
    // Инициализация структуры перенаправления
    redir_info->input_file = NULL;
    redir_info->output_file = NULL;
    redir_info->append = 0;
    redir_info->syntax_error = 0;
    
    while (token != NULL && argc < 63) {
        // Проверяем перенаправление ввода
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            if (token == NULL || redir_info->input_file != NULL) {
                redir_info->syntax_error = 1;
                return 0;
            }
            redir_info->input_file = token;
        }
        // Проверяем перенаправление вывода
        else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n");
            if (token == NULL || redir_info->output_file != NULL) {
                redir_info->syntax_error = 1;
                return 0;
            }
            redir_info->output_file = token;
            redir_info->append = 0;
        }
        // Проверяем перенаправление вывода с добавлением
        else if (strcmp(token, ">>") == 0) {
            // >> не поддерживается согласно тестам
            redir_info->syntax_error = 1;
            return 0;
        }
        // Обычный аргумент
        else {
            args[argc++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    args[argc] = NULL;
    return argc;
}

/**
 * @brief Разбиение строки на аргументы
 *
 * @param line Входная строка для разбора
 * @param args Массив для хранения аргументов
 * @return int Количество найденных аргументов
 */
int parse_args(char *line, char **args) {
    redirection_info_t redir_info;
    return parse_args_with_redirection(line, args, &redir_info);
}

/**
 * @brief Функция для разделения команд по &&
 *
 * @param cmd Входная строка команды
 * @param commands Массив для хранения разделенных команд
 * @param cmd_count Указатель на переменную для хранения количества команд
 */
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

/**
 * @brief Запуск одной команды через vfork с поддержкой перенаправления
 *
 * @param args Массив аргументов команды
 * @param argc Количество аргументов
 * @param redir_info Структура с информацией о перенаправлении
 * @return int 1 если команда выполнена успешно, 0 если произошла ошибка
 */
int run_single_command_with_redirection(char **args, int argc, redirection_info_t *redir_info) {
    if (argc == 0) {
        // Проверяем синтаксические ошибки
        if (redir_info->syntax_error) {
            printf("Syntax error\n");
            return 0;
        }
        // Проверяем ошибки ввода/вывода
        if (redir_info->input_file != NULL) {
            FILE *file = fopen(redir_info->input_file, "r");
            if (file == NULL) {
                printf("I/O error\n");
                return 0;
            }
            fclose(file);
        }
        if (redir_info->output_file != NULL) {
            FILE *file = fopen(redir_info->output_file, "w");
            if (file == NULL) {
                printf("I/O error\n");
                return 0;
            }
            fclose(file);
        }
        return 1; // Пустая команда - успех
    }
    
    // Проверяем синтаксические ошибки
    if (redir_info->syntax_error) {
        printf("Syntax error\n");
        return 0;
    }
    
    // Обработка cd в родительском процессе (не через vfork)
    if (strcmp(args[0], "cd") == 0) {
        cmd_cd(argc > 1 ? args[1] : NULL);
        return 1;
    }
    
    pid_t pid = vfork();
    if (pid == -1) {
        printf("Error: vfork failed\n");
        return 0;
    }
    
    if (pid == 0) {
        // Дочерний процесс - настраиваем перенаправление через dup2
        int input_fd = -1;
        int output_fd = -1;
        
        // Открываем файл для ввода
        if (redir_info->input_file != NULL) {
            input_fd = open(redir_info->input_file, O_RDONLY);
            if (input_fd == -1) {
                printf("I/O error\n");
                _exit(1);
            }
            if (dup2(input_fd, STDIN_FILENO) == -1) {
                printf("I/O error\n");
                _exit(1);
            }
            close(input_fd);
        }
        
        // Открываем файл для вывода
        if (redir_info->output_file != NULL) {
            if (redir_info->append) {
                output_fd = open(redir_info->output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            } else {
                output_fd = open(redir_info->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            }
            if (output_fd == -1) {
                printf("I/O error\n");
                _exit(1);
            }
            if (dup2(output_fd, STDOUT_FILENO) == -1) {
                printf("I/O error\n");
                _exit(1);
            }
            close(output_fd);
        }
        
        // Выполняем команду
        if (execute_builtin(args, argc, redir_info)) {
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

/**
 * @brief Запуск одной команды через vfork
 *
 * @param args Массив аргументов команды
 * @param argc Количество аргументов
 * @return int 1 если команда выполнена успешно, 0 если произошла ошибка
 */
int run_single_command(char **args, int argc) {
    redirection_info_t redir_info = {0};
    return run_single_command_with_redirection(args, argc, &redir_info);
}

/**
 * @brief Основная логика выполнения команд с поддержкой &&
 *
 * @param cmd Входная строка команды
 */
void execute_command(char *cmd) {
    // Удаляем пробелы в начале/конце
    while (*cmd == ' ') cmd++;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && *end == ' ') *end-- = '\0';
    if (*cmd == '\0') {
        return;
    }

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

        if (*cmd_start == '\0') {
            continue;
        }

        // Парсим аргументы с учётом перенаправления
        char *args[64] = {0};
        redirection_info_t redir_info = {0};
        int argc = parse_args_with_redirection(cmd_start, args, &redir_info);

        // Запускаем команду через vfork с перенаправлением
        should_continue = run_single_command_with_redirection(args, argc, &redir_info);
    }

    // Освобождаем память
    for (int i = 0; i < cmd_count; i++) {
        free(commands[i]);
    }
}

/**
 * @brief Смена текущей директории
 *
 * @param path Путь к новой директории (если NULL, используется домашняя директория)
 */
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

/**
 * @brief Вывод текущей рабочей директории
 */
void cmd_pwd() {
    char buf[1024];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        printf("%s\n", buf);
    } else {
        printf("pwd: %s\n", strerror(errno));
    }
}

/**
 * @brief Вывод содержимого директории
 *
 * @param path Путь к директории (если NULL, используется текущая директория)
 */
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

/**
 * @brief Вывод текста на экран с поддержкой перенаправления
 *
 * @param args Массив аргументов команды
 * @param argc Количество аргументов
 * @param redir_info Информация о перенаправлении
 */
void cmd_echo(char **args, int argc, redirection_info_t *redir_info) {
    if (argc < 2) {
        printf("\n");
        return;
    }
    
    FILE *output = stdout;
    
    // Используем информацию о перенаправлении из redir_info
    if (redir_info != NULL && redir_info->output_file != NULL) {
        output = fopen(redir_info->output_file, redir_info->append ? "a" : "w");
        if (!output) {
            printf("I/O error\n");
            return;
        }
    }
    
    // Выводим все аргументы (кроме команды echo)
    for (int i = 1; i < argc; i++) {
        fprintf(output, "%s", args[i]);
        if (i < argc - 1) {
            fprintf(output, " ");
        }
    }
    fprintf(output, "\n");
    
    if (redir_info != NULL && redir_info->output_file != NULL) {
        fclose(output);
        // НЕ выводим ничего в stdout при перенаправлении в файл
        return;
    }
}

/**
 * @brief Вывод полного пути к файлу или директории
 *
 * @param path Путь к файлу или директории
 */
void cmd_short_path(char *path) {
    char buf[4096];
    if (realpath(path, buf) != NULL) {
        printf("%s\n", buf);
    } else {
        printf("short-path: %s: %s\n", path, strerror(errno));
    }
}

/**
 * @brief Очистка экрана
 */
void cmd_cls() {
    printf("\033[H\033[J");
}

/**
 * @brief Вывод справки по доступным командам
 */
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

/**
 * @brief Основная функция программы - точка входа в оболочку
 *
 * @return int Код возврата программы
 */
int main(int argc, char *argv[]) {
    char command[1024] = {0};

    // Если передана команда как аргумент, используем её
    if (argc > 1) {
        // Собираем все аргументы в одну строку
        int pos = 0;
        for (int i = 1; i < argc && pos < sizeof(command) - 1; i++) {
            if (i > 1 && pos < sizeof(command) - 2) {
                command[pos++] = ' ';
            }
            int arg_len = strlen(argv[i]);
            if (pos + arg_len < sizeof(command) - 1) {
                strcpy(command + pos, argv[i]);
                pos += arg_len;
            } else {
                // Обрезаем, если команда слишком длинная
                strncpy(command + pos, argv[i], sizeof(command) - pos - 1);
                command[sizeof(command) - 1] = '\0';
                break;
            }
        }
        execute_command(command);
    } else {
        // Интерактивный режим (для тестирования)
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
    }
    return 0;
}