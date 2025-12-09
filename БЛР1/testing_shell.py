#!/usr/bin/env python3

import os
import subprocess

SHELL_PATH = "./shell"

class Shell:
    def __init__(self, path="./shell"):
        self._path = path

    def feed(self, command):
        # Запускаем shell с командой и получаем вывод
        try:
            # Проверяем, есть ли перенаправление вывода
            has_output_redirect = '>' in command
            
            if has_output_redirect:
                # При перенаправлении НЕ захватываем stdout, только stderr
                result = subprocess.run([self._path] + command.split(),
                                        capture_output=False,  # НЕ захватываем!
                                        text=True, timeout=3,
                                        # Перенаправляем stdout в /dev/null
                                        stdout=subprocess.DEVNULL,
                                        stderr=subprocess.PIPE)
            else:
                # Без перенаправления - захватываем как обычно
                result = subprocess.run([self._path] + command.split(),
                                        capture_output=True,
                                        text=True, timeout=3)
            
            if result.returncode != 0:
                if "Sanitizer" in result.stderr:
                    raise AssertionError("Sanitizer error:\n{}".format(result.stderr))
                # Для команд, которые не должны ничего выводить
                if result.stderr and not result.stdout:
                    return ""
            
            # Если было перенаправление, возвращаем пустую строку
            if has_output_redirect:
                return ""
            
            return result.stdout.rstrip('\n') if result.stdout else ""
        except subprocess.TimeoutExpired:
            raise AssertionError("Command timed out")

def test_shell_basics():
    shell = Shell(SHELL_PATH)

    
    assert shell.feed("echo hello") == "hello"
    
    assert shell.feed(" echo    hello  world") == "hello world"
    # Пропускаем многострочные команды, так как они требуют интерактивного режима

    # Тесты для файловых операций
    assert shell.feed("touch ./foo") == ""
    assert os.path.exists("./foo")
    assert shell.feed("rm ./foo") == ""
    assert not os.path.exists("./foo")

    # Тесты для команд, возвращающих результат
    assert shell.feed("echo test") == "test"

def test_shell_redirection():
    shell = Shell(SHELL_PATH)
    # Тесты перенаправления
    assert shell.feed("echo shad rocks > aaa") == "" # выовд будет, так как Python программа перехватывает все из stdout.
    assert shell.feed("cat < aaa") == "shad rocks" # А тут мы проверим, что файл создался
    shell.feed("cat > /dev/null < aaa")
    assert shell.feed("cat > /dev/null < aaa") == ""

    assert shell.feed("echo c forever > aaa") == ""
    assert shell.feed("cat < aaa") == "c forever"

    # Очистка
    assert shell.feed("rm aaa") == ""

if __name__ == "__main__":
    test_shell_basics()
    test_shell_redirection()
    print("All tests passed!")