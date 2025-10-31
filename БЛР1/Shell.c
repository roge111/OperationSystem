#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

void execute_command(char* cmd) {

    
    while (*cmd == ' ') cmd++;
    char *end = cmd + strlen(cmd) - 1;
    while (end > cmd && *end == ' ') *end-- = '\0';
    
    if (strlen(cmd) == 0) return;
    
    pid_t pid = vfork();
    
    if (pid == 0) {
        freopen("/dev/null", "w", stderr);
        execlp("sh", "sh", "-c", cmd, NULL);
        exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            printf("Command not found\n");
        }
    }
}

void execute_with_stdin(char* cmd, char* input) {
    int pipefd[2];
    pipe(pipefd);
    
    pid_t pid = fork();
    
    if (pid == 0) {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        
        freopen("/dev/null", "w", stderr);
        execlp("sh", "sh", "-c", cmd, NULL);
        exit(127);
    } else if (pid > 0) {
        close(pipefd[0]);
        write(pipefd[1], input, strlen(input));
        close(pipefd[1]);
        
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            printf("Command not found\n");
        }
    }
}

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
        
        // Определяем тип: multiple commands или команда с stdin
        int newline_count = 0;
        for (int i = 0; processed[i] != '\0'; i++) {
            if (processed[i] == '\n') newline_count++;
        }
        
        if (newline_count >= 2) {
            // Вероятно команда с stdin (cat\nhello\nworld)
            char *first_nl = strchr(processed, '\n');
            if (first_nl != NULL) {
                *first_nl = '\0';
                char *cmd = processed;
                char *input = first_nl + 1;
                execute_with_stdin(cmd, input);
            }
        } else {
            // Multiple commands (echo hello\necho world)
            // Заменяем \n на ;
            for (int i = 0; processed[i] != '\0'; i++) {
                if (processed[i] == '\n') processed[i] = ';';
            }
            execute_command(processed);
        }
    }
    
    return 0;
}