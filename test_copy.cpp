#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <queue>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "libcaesar.h"

extern "C" {
void set_key(char k);
void caesar(void *src, void *dst, int len);
void init_secure_crypto();
void test();
}

int main() {

    init_secure_crypto();

    set_key(0x55);

    /*
     * Демонстрация защиты памяти
     */
    test();

    cleanup_secure_crypto();

    return 0;
}