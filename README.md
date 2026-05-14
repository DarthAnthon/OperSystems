# Общее описание

Проект содержит обновлённую библиотеку libcaesar, содержащую функции для установки ключа и шифрования/дешифрования, программу secure_copy, Makefile для сборки проекта.

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

<img width="1082" height="558" alt="image" src="https://github.com/user-attachments/assets/7d82c391-fb0a-457c-afe0-2be208104db9" />

