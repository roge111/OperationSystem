#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void main(){
    char command[100]; //Выделил память под введение команды
    fgets(command, sizeof(command), stdin); //Считаваем всю строку
    command[strcspn(command, "\n")] = '\0';

    char operation[] = "AND";
    char new[] = "&& ";
    char *pos;
    // Цикл while производит замену AND на &&.
    // strncpy - производит копирование new на pos, а так как pos - это указатель на место в command, 
    // то вставляется new именно на его место
    while ((pos = strstr(command, operation))  != NULL) {
        strncpy(pos, new, strlen(new));
        pos += strlen(new);
    }
    
    int result = system(command);
    if (result == -1){
        printf("Завершилось с ошибкой\n");
    } else {
        printf("Все в порядке\n");
    }
    
}
    