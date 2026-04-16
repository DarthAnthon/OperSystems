# Общее описание

Проект содержит библиотеку libcaesar, содержащую функции для установки ключа и шифрования/дешифрования, программу secure_copy, Makefile для сборки проекта.

# Описание функций

## Библиотека libcaesar:

void set_key(char key) - устанавливает ключ

void caesar(void* src, void* dst, int len) - производит шифрование/дешифрования содержимого буфера src и записывает результат в буфер dsr

## Программа secure_copy:

int main(int argc, char* argv[]) - основная функция программы, производит динамическую загрузку библиотеки, имеет два режима работы: параллельная и последовательная обработка файлов.

# Описание команд

make - сборка проекта

./secure_copy file1.txt file2.txt file3.txt output_dir/ X - шифрование с ключом X содержимого файлов и запись результата в папку output_dir

./secure_copy --mode=parallel file1.txt file2.txt file3.txt output_dir/ X - шифрование с ключом X содержимого файлов и запись результата в папку output_dir с выбранным режимом работы - параллельная обработка (--mode=sequential для последовательной)

# Примеры использования

<img width="1079" height="463" alt="image" src="https://github.com/user-attachments/assets/d583d792-4edc-43c1-b0df-e0d02936a5cf" />
