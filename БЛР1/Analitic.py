import subprocess
import sys
import os


def run_c_loader_args(count_cpu, count_memory, memory_number=None):
    """
    Запускает C программу с передачей параметров через аргументы
    """
    # Формируем команду
    cmd = ['./loaders', str(count_cpu), str(count_memory)]
    
    if count_memory > 0 and memory_number is not None:
        cmd.append(str(memory_number))
    
    # Запускаем процесс
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True
    )
    
    # Выводим результат
    
    
    if result.stderr:
        print("STDERR:")
        print(result.stderr)
    
    
    return result.returncode, result.stdout

# Примеры использования
if __name__ == "__main__":

    file_name = input('Введите название файла для сохрания без формата файла>> ')
    
    if os.path.exists(file_name):
        file_open = open(f'{file_name}.csv', 'a')
    else:
        file_open = open(f'{file_name}.csv', 'w')
        file_open.write('Количество нагрузки на CPU; Количетсво нагрузки на память; Время работы;\n')

    count_cpu_loaders = int(input('Введите количество нагрузчиков на cpu>> '))
    count_memory_loaders = int(input('Ввеодите количество нагрузчиков на память>> '))
    if count_memory_loaders > 0:
        number_for_memory = int(input('Введите число для нагрузчика на память>> '))

    number_launches = int(input('Введите число запусков для анализа>> '))
    sum_times = 0
    for i in range(number_launches):

        if count_memory_loaders == 0:
            code, result = run_c_loader_args(count_cpu_loaders, 0)
        else:
            code, result = run_c_loader_args(count_cpu_loaders, count_memory_loaders, number_for_memory)
        print(f'Общее время выполнения: {result}. Запуск номер {i+1}')
        result = int(result.strip())
        sum_times += result
    print(f'Среднее значение времени: {sum_times/number_launches}')
    file_open.write(f'{count_cpu_loaders};{count_memory_loaders};{sum_times/number_launches}\n')
