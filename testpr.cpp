#include <iostream>
#include <fstream>
#include <vector>
#include <dlfcn.h>

// Типы указателей на функции set_key и caesar из библиотеки libcaesar.so 
typedef void (*set_key_func)(char);
typedef void (*caesar_func)(void*, void*, int);

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0]
            << " <libpath> <key> <input> <output>\n";
        return 1;
    }

    const char* libpath = argv[1];
    char key = argv[2][0];          
    const char* inputFile = argv[3];
    const char* outputFile = argv[4];

    // 1. Динамическая загрузка библиотеки
    void* handle = dlopen(libpath, RTLD_LAZY);
    if (!handle) {
        std::cerr << "dlopen error: " << dlerror() << '\n';
        return 1;
    }

    // 2. Получение адресов функций
    set_key_func set_key = (set_key_func)dlsym(handle, "set_key");
    caesar_func caesar = (caesar_func)dlsym(handle, "caesar");
    if (!set_key || !caesar) {
        std::cerr << "dlsym error: " << dlerror() << '\n';
        dlclose(handle);
        return 1;
    }

    // 3. Чтение входного файла целиком
    std::ifstream in(inputFile, std::ios::binary | std::ios::ate);
    if (!in) {
        std::cerr << "Cannot open input file: " << inputFile << '\n';
        dlclose(handle);
        return 1;
    }
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<char> buffer(size);
    if (!in.read(buffer.data(), size)) {
        std::cerr << "Error reading input file\n";
        dlclose(handle);
        return 1;
    }
    in.close();

    // 4. Установка ключа и шифрование (на месте)
    set_key(key);
    caesar(buffer.data(), buffer.data(), size);

    // 5. Запись результата
    std::ofstream out(outputFile, std::ios::binary);
    if (!out.write(buffer.data(), size)) {
        std::cerr << "Error writing output file\n";
        dlclose(handle);
        return 1;
    }
    out.close();

    std::cout << "Successfully processed " << size << " bytes.\n";
    dlclose(handle);
    return 0;
}