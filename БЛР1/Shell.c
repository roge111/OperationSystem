#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>



// Изменить выполнение команд. 
// Есть список, пишем код, который выполняет команды не через exec.Если команда не из списка, тогда выполняем через exec.
// Списко уточнить у одногруппников

/**
 * @brief Выполняет команду оболочки
 *
 * Эта функция создает дочерний процесс и выполняет указанную команду оболочки.
 * Она удаляет пробелы в начале и конце команды перед выполнением.
 * Если команда пуста, функция возвращает управление без выполнения.
 * В случае ошибки "command not found" выводится соответствующее сообщение.
 * Другие ошибки выполнения (например, отсутствие файла) не обрабатываются.
 *
 * @param cmd Указатель на строку с командой для выполнения
 */
void execute_command(char* cmd) {
    while (*cmd == ' ') cmd++;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && *end == ' ') *end-- = '\0';
    
    if (strlen(cmd) == 0) return;
    
    pid_t pid = vfork();
    if (pid == -1) {
        printf("Error: vfork failed\n");
        return;
    }
    
    if (pid == 0) {
        execlp("sh", "sh", "-c", cmd, NULL);
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            if (exit_status == 127) {
                printf("Command not found\n");
            }
        }
    }
}

/**
 * @brief Выполняет команду оболочки с переданным стандартным вводом
 *
 * Эта функция создает дочерний процесс и выполняет указанную команду оболочки,
 * передавая ей данные через стандартный ввод через pipe.
 * Используется только для команд с stdin данными (формат: команда\nданные).
 *
 * @param cmd Указатель на строку с командой для выполнения
 * @param input Указатель на строку с данными для передачи в стандартный ввод команды
 */
void execute_with_stdin(char* cmd, char* input) {
    int pipefd[2];
    pipe(pipefd);
    
    pid_t pid = vfork();
    
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        
        execlp("sh", "sh", "-c", cmd, NULL);
        exit(127);
    } else if (pid > 0) {
        close(pipefd[0]);
        write(pipefd[1], input, strlen(input));
        close(pipefd[1]);
        
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            printf("Command not found\n");
        }
    }
}

/**
 * @brief Основная функция программы оболочки
 *
 * Эта функция читает команду из стандартного ввода, обрабатывает ее
 * (заменяет AND на &&, обрабатывает \n последовательности)
 * и определяет тип команды на основе количества символов новой строки:
 * - 0-1 \n: multiple commands (заменяются на ;)
 * - >=2 \n: команда с stdin данными
 * Все команды с пробелами выполняются как обычные команды с аргументами.
 *
 * @return Возвращает 0 при успешном завершении
 */
int main() {
    char command[1024];
    
    if (fgets(command, sizeof(command), stdin) != NULL) {
        command[strcspn(command, "\n")] = '\0';
        
        // Заменяем AND на &&
        char *pos;
        while ((pos = strstr(command, "AND")) != NULL) {
            strncpy(pos, "&&", 2);
            memmove(pos + 2, pos + 3, strlen(pos + 3) + 1);
        }
        
        // Обрабатываем \n последовательности
        char processed[1024] = {0};
        int j = 0;
        for (int i = 0; command[i] != '\0' && j < sizeof(processed) - 1; i++) {
            if (command[i] == '\\' && command[i+1] == 'n') {
                processed[j++] = '\n';
                i++;
            } else {
                processed[j++] = command[i];
            }
        }
        
        // Определяем тип команды по количеству символов новой строки
        int newline_count = 0;
        for (int i = 0; processed[i] != '\0'; i++) {
            if (processed[i] == '\n') newline_count++;
        }
        
        if (newline_count >= 2) {
            // Команда с stdin данными (cat\nhello\nworld)
            char *first_nl = strchr(processed, '\n');
            if (first_nl != NULL) {
                *first_nl = '\0';
                char *cmd = processed;
                char *input = first_nl + 1;
                execute_with_stdin(cmd, input);
            }
        } else {
            // Все остальные случаи: одиночные команды, команды с аргументами, multiple commands
            for (int i = 0; processed[i] != '\0'; i++) {
                if (processed[i] == '\n') processed[i] = ';';
            }
            execute_command(processed);
        }
    }
    
    return 0;
}