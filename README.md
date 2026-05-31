# Общее описание

Проект содержит обновлённую библиотеку rc4, содержащую функции для установки ключа и шифрования/дешифрования, программу secure_copy, Makefile для сборки проекта и run_tests.sh, содержащий тесты работы программы.

# Описание функций

## Библиотека rc4:

void rc4_init() - Инициализация библиотеки

int rc4_state_init(const unsigned char *master_key, size_t master_len, const unsigned char *salt) - Инициализация состояния: KSA + сброс индексов i, j; Память защищается PROT_NONE после инициализации.

int rc4_state_update(int slot, const unsigned char *input, unsigned char *output, size_t length) - Обработка чанка: PRGA с сохранением i, j в защищённой памяти

void rc4_state_free(int slot) - Освобождение состояния: безопасное затирание и закрытие слота

void rc4_crypt(const unsigned char *master_key, size_t master_len, const unsigned char *salt, const unsigned char *input, unsigned char *output, size_t length) - Legacy wrapper для однократного вызова

## Программа secure_copy:

int main(int argc, char* argv[]) - основная функция программы, определяет команду и выполняет её.

# Описание команд

make - сборка проекта

./secure_copy -list -image disk.img - Список файлов в образе

./secure_copy -add -image disk.img -key "secret" команды.txt - Добавление файлов в образ

./secure_copy -get -image disk.img -key "secret" -out result.txt команды.txt - Извлечение и расшифровка файла

./run_tests.sh - Запуск тестов

# Примеры использования

<img width="1482" height="300" alt="image" src="https://github.com/user-attachments/assets/9d801c28-b1a3-4985-911a-a80ea787f6b3" />



