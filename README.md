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

./secure_copy file1.txt file2.txt file3.txt output_dir/ X - шифрование с ключом X содержимого файлов и запись результата в папку output_dir

# Примеры использования

<img width="1082" height="279" alt="image" src="https://github.com/user-attachments/assets/1fcc318f-ac37-41ee-b39a-41b92ea7d678" />
