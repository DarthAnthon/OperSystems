#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>

#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libcaesar.h"

static void *protected_key = nullptr;
static size_t page_size = 0;

static struct sigaction old_sigsegv;

/*
 * Глобальный mutex
 */
static std::mutex key_mutex;

/*
 * Флаг инициализации
 */
static bool initialized = false;

/*
 * Обработчик SIGSEGV
 */
void segv_handler(int sig, siginfo_t *info, void *context) {

  uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
  uintptr_t start = reinterpret_cast<uintptr_t>(protected_key);

  if (fault >= start && fault < start + page_size) {
    const char msg[] = "\n[SECURITY ERROR] Illegal access to protected memory!\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    emergency_cleanup();
    _exit(EXIT_FAILURE);
  }

  signal(SIGSEGV, SIG_DFL);
  raise(SIGSEGV);
}

/*
 * Установка обработчика SIGSEGV
 */
void install_segv_handler() {

  struct sigaction sa;

  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = segv_handler;

  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGSEGV, &sa, &old_sigsegv) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
}

/*
 * Выделение памяти
 */
void allocate_protected_key() {

  page_size = sysconf(_SC_PAGESIZE);

  protected_key = mmap(nullptr, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (protected_key == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }
}

/*
 * Безопасное уничтожение ключа
 */
void destroy_key() {

  if (!protected_key)
    return;

  /*
   * Разрешаем чтение/запись
   */
  if (mprotect(protected_key, page_size, PROT_READ | PROT_WRITE) == -1) {

    perror("mprotect");
    exit(EXIT_FAILURE);
  }

  /*
   * Затираем память
   */
  memset(protected_key, 0, page_size);

  /*
   * Закрываем доступ
   */
  if (mprotect(protected_key, page_size, PROT_NONE) == -1) {

    perror("mprotect");
    exit(EXIT_FAILURE);
  }

  /*
   * Освобождаем память
   */
  if (munmap(protected_key, page_size) == -1) {
    perror("munmap");
    exit(EXIT_FAILURE);
  }

  protected_key = nullptr;
}

extern "C" {

/*
 * Инициализация
 */
void init_secure_crypto() {

  std::lock_guard<std::mutex> lock(key_mutex);

  if (initialized)
    return;

  install_segv_handler();

  allocate_protected_key();

  atexit(cleanup_secure_crypto);

  initialized = true;
}

/*
 * Установка ключа
 */
void set_key(char k) {

  std::lock_guard<std::mutex> lock(key_mutex);

  if (!initialized) {
    std::cerr << "Library not initialized\n";
    exit(EXIT_FAILURE);
  }

  /*
   * Разрешаем запись
   */
  if (mprotect(protected_key, page_size, PROT_READ | PROT_WRITE) == -1) {

    perror("mprotect");
    exit(EXIT_FAILURE);
  }

  /*
   * Запись ключа
   */
  memcpy(protected_key, &k, sizeof(char));

  /*
   * Запрещаем доступ
   */
  if (mprotect(protected_key, page_size, PROT_NONE) == -1) {

    perror("mprotect");
    exit(EXIT_FAILURE);
  }
}

/*
 * Шифрование
 */
void caesar(void *src, void *dst, int len) {

  std::lock_guard<std::mutex> lock(key_mutex);

  if (mprotect(protected_key, page_size, PROT_READ) == -1) {

    perror("mprotect");
    exit(EXIT_FAILURE);
  }

  volatile char *key = static_cast<volatile char *>(protected_key);

  char *s = static_cast<char *>(src);
  char *d = static_cast<char *>(dst);

  for (int i = 0; i < len; ++i) {
    d[i] = s[i] ^ key[0];
  }

  if (mprotect(protected_key, page_size, PROT_NONE) == -1) {

    perror("mprotect");
    exit(EXIT_FAILURE);
  }
}

/*
 * Очистка ресурсов
 */
void emergency_cleanup() {

  if (!protected_key)
    return;

  mprotect(protected_key, page_size, PROT_READ | PROT_WRITE);

  memset(protected_key, 0, page_size);

  mprotect(protected_key, page_size, PROT_NONE);

  munmap(protected_key, page_size);

  protected_key = nullptr;
}

void cleanup_secure_crypto() {

  std::lock_guard<std::mutex> lock(key_mutex);

  if (!initialized)
    return;

  emergency_cleanup();

  initialized = false;
}

void test(){
	std::lock_guard<std::mutex> lock(key_mutex);

    if (!initialized) {
        std::cerr << "Library not initialized\n";
        exit(EXIT_FAILURE);
    }

    /*
     * Убеждаемся, что память закрыта
     */
    if (mprotect(
            protected_key,
            page_size,
            PROT_NONE) == -1) {

        perror("mprotect");
        exit(EXIT_FAILURE);
    }

    /*
     * Намеренная ошибка доступа
     */
    volatile char* ptr =
        static_cast<volatile char*>(protected_key);

    ptr[0] = 123;

}
}
