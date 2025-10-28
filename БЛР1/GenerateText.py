import random
import string

def generate_fragment():
    length = random.randint(100, 150)
    
    # Список допустимых типов символов
    char_types = [
        # string.ascii_letters,  # Буквы
        string.digits,         # Цифры
        # string.punctuation,    # Спецсимволы
        # ''.join(chr(i) for i in range(0x10000) if chr(i).isprintable()),  # Unicode символы
        # '𝄞𝄟𝄠𝄡𝄢'  # Сложные эмодзи
    ]
    
    fragment = []
    for _ in range(length):
        char_type = random.choice(char_types)
        try:
            fragment.append(random.choice(char_type))
        except Exception:
            continue  # Пропускаем проблемные символы
    
    return ''.join(fragment)

def generate_fragments(count):
    return [generate_fragment() for _ in range(count)]

def save_fragments_to_file(fragments, filename):
    with open(filename, 'w', encoding='utf-8') as f:
        for frag in fragments:
            try:
                f.write(frag + '\n')
            except UnicodeEncodeError:
                # Очищаем строку от недопустимых символов
                cleaned_frag = ''.join(c for c in frag if ord(c) < 0x10FFFF and not ('\ud800' <= c <= '\udfff'))
                f.write(cleaned_frag + '\n')

# Генерация 100 000 фрагментов
fragments = generate_fragments(10000)

# Сохранение в файл
save_fragments_to_file(fragments, 'fragments_numbers.txt')

print(f"Сгенерировано {len(fragments)} фрагментов")
