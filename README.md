# Общее описание

Проект содержит библиотеку libcaesar, содержащую функции для установки ключа и шифрования/дешифрования, программу secure_copy, Makefile для сборки проекта.

# Описание функций

## Библиотека libcaesar:

void set_key(char key) - устанавливает ключ

void caesar(void* src, void* dst, int len) - производит шифрование/дешифрования содержимого буфера src и записывает результат в буфер dsr

## Программа secure_copy:

int main(int argc, char* argv[]) - основная функция программы, производит динамическую загрузку библиотеки, создаёт два потока для чтения в одном и шифрования с записью в другом.

# Описание команд

make - сборка проекта

make test - запуск теста

./secure_copy source.txt dest.txt X - шифрование с ключом X содержимого файла source.txt и запись результата в dest.txt

# Примеры использования

<img width="1111" height="181" alt="image" src="https://github.com/user-attachments/assets/e44f13cf-3dcf-4825-b08a-e606984341dc" />
