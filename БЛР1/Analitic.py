import subprocess
import sys
import os


def run_c_loader_args(count_cpu, count_memory, memory_number=None):
    """
    Запускает C программу с передачей параметров через аргументы
    """
    # Формируем команду
    cmd = ['./loaders_o', str(count_cpu), str(count_memory)]
    
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
        file_open.write('Количество нагрузки на CPU,Количетсво нагрузки на память,ремя работы,%USER,%SYS,%WAIT,Количество переключений контекста,Количество паралельных процессов\n')
    print('ЕСЛИ ЗАХОТИТЕ ПРОВЕСТИ АНАЛИЗ НА ОДНОВРЕМЕННУЮ РАБОТУ РАЗНОГО ЧИСЛА НАГРУЗЧИКОВ, ТО ВВЕИДЕТ ПО 1')
    count_cpu_loaders = int(input('Введите количество нагрузчиков на cpu>> '))
    count_memory_loaders = int(input('Ввеодите количество нагрузчиков на память>> '))
    if count_memory_loaders > 0:
        number_for_memory = int(input('Введите число для нагрузчика на память>> '))

    number_launches = int(input('Введите число запусков для анализа>> '))
    if count_cpu_loaders and not(count_memory_loaders):
        
        cnt = 0 # Число увеличений нагрузок на cpu
        count_steps = int(input("Введите количество увелечений (шагов) нагрузок на CPU: "))
        step  = int(input("Введите шаг увеличения нагрузок на CPU: "))
        while cnt < count_steps:
            sum_times = 0
            sum_system = 0
            sum_user = 0
            sum_wait = 0
            sum_context_switches = 0
            sum_processes = 0
            print('Шаг', cnt)
            for i in range(number_launches):

                
                code, result = run_c_loader_args(count_cpu_loaders, 0)
                
                print(result, f'Запуск: {i + 1}')
                result = list(map(float, result.split(',')))
                sum_times += result[0]
                sum_user += result[1]
                sum_system += result[2]
                sum_wait += result[3]
                sum_context_switches += result[4]
                sum_processes += result[5]
            file_open.write(f'{count_cpu_loaders},{count_memory_loaders},{sum_times/number_launches},{sum_user/number_launches},{sum_system/number_launches};{sum_wait/number_launches},{sum_context_switches/number_launches},{sum_processes/number_launches}\n')
            count_cpu_loaders += step
            cnt +=1
    elif count_memory_loaders and not(count_cpu_loaders):
        sum_times = 0
        cnt = 0 # Число увеличений нагрузок на cpu
        count_steps = int(input("Введите количество увелечений (шагов) нагрузок на память: "))
        step  = int(input("Введите шаг увеличения нагрузок на память: "))
        while cnt < count_steps:
            sum_times = 0
            sum_system = 0
            sum_user = 0
            sum_wait = 0
            sum_context_switches = 0
            sum_processes = 0
            print('Шаг', cnt)
            for i in range(number_launches):

                
                code, result = run_c_loader_args(count_cpu_loaders, count_memory_loaders, number_for_memory)
                
                print(result, f'Запуск: {i + 1}')
                result = list(map(float, result.split(',')))
                sum_times += result[0]
                sum_user += result[1]
                sum_system += result[2]
                sum_wait += result[3]
                sum_context_switches += result[4]
                sum_processes += result[5]
            file_open.write(f'{count_cpu_loaders},{count_memory_loaders},{sum_times/number_launches},{sum_user/number_launches},{sum_system/number_launches};{sum_wait/number_launches},{sum_context_switches/number_launches},{sum_processes/number_launches}\n')
            count_memory_loaders += step
            cnt +=1
    else:

        steps = [
                (20, 1),    # базовый CPU‑тест при минимальной памяти
                (20, 10),  # умеренная память при пороге CPU
                (20, 20),  # равные нагрузки на CPU и память
                (30, 10),  # CPU выше порога, низкая память
                (30, 50),  # средний уровень памяти при повышенном CPU
                (40, 20),  # высокий CPU, средняя память
                (40, 75),  # почти максимальная память при высоком CPU
                (50, 30),  # пиковый CPU, умеренная память
                (50, 75),  # высокая нагрузка на оба ресурса
                (50, 100)  # максимальная нагрузка на память и CPU
        ]
        cnt = 0
        for count_cpu_loaders, count_memory_loaders in steps:
            cnt += 1
            print(f'Количество нагрузчиков на CPU: {count_cpu_loaders}')
            print(f'Количество нагрузчиков на память: {count_memory_loaders}')
            print(f'Запуск {cnt} из {len(steps)}')
            sum_times = 0
            sum_system = 0
            sum_user = 0
            sum_wait = 0
            sum_context_switches = 0
            sum_processes = 0
            for i in range(number_launches):

                
                code, result = run_c_loader_args(count_cpu_loaders, count_memory_loaders, number_for_memory)
                print(result, f'Запуск: {i + 1}')
                result = list(map(float, result.split(',')))
                sum_times += result[0]
                sum_user += result[1]
                sum_system += result[2]
                sum_wait += result[3]
                sum_context_switches += result[4]
                sum_processes += result[5]

            print(f'Среднее значение времени: {sum_times/number_launches}')
            file_open.write(f'{count_cpu_loaders},{count_memory_loaders},{sum_times/number_launches},{sum_user/number_launches},{sum_system/number_launches};{sum_wait/number_launches},{sum_context_switches/number_launches},{sum_processes/number_launches}\n')
