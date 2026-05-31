#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include "rc4.h"

#define MAX_THREADS 5

struct ProtectedRC4State {
    unsigned char S[256];
    unsigned char key[272];
    size_t key_len;
    int i;          // Индекс PRGA i (сохраняется между чанками)
    int j;          // Индекс PRGA j (сохраняется между чанками)
    bool in_use;
};

struct SlotState {
    void *memory;
    bool in_use;
    std::mutex mutex;
};

static SlotState protected_pool[MAX_THREADS];
static size_t page_size = 0;
static struct sigaction old_sigsegv;
static std::mutex init_mutex;
static bool initialized = false;

static void rc4_cleanup_protected_memory();

void rc4_segv_handler(int sig, siginfo_t *info, void *context) {
    uintptr_t fault = reinterpret_cast<uintptr_t>(info->si_addr);
    bool is_protected = false;
    for (int i = 0; i < MAX_THREADS; i++) {
        if (protected_pool[i].memory) {
            uintptr_t start = reinterpret_cast<uintptr_t>(protected_pool[i].memory);
            if (fault >= start && fault < start + page_size) {
                is_protected = true;
                break;
            }
        }
    }
    if (is_protected) {
        const char msg[] = "\n[RC4 SECURITY VIOLATION] Unauthorized access to protected memory!\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        rc4_cleanup_protected_memory();
        _exit(EXIT_FAILURE);
    }
    if (old_sigsegv.sa_flags & SA_SIGINFO && old_sigsegv.sa_sigaction) {
        old_sigsegv.sa_sigaction(sig, info, context);
    } else if (old_sigsegv.sa_handler != SIG_DFL && old_sigsegv.sa_handler != SIG_IGN) {
        old_sigsegv.sa_handler(sig);
    } else {
        signal(SIGSEGV, SIG_DFL);
        raise(SIGSEGV);
    }
}

static void rc4_install_segv_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = rc4_segv_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, &old_sigsegv) == -1) {
        perror("[RC4 ERROR] sigaction failed");
        exit(EXIT_FAILURE);
    }
}

static void* rc4_allocate_protected_memory() {
    void* mem = mmap(nullptr, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("[RC4 ERROR] mmap failed");
        return nullptr;
    }
    return mem;
}

static void rc4_destroy_protected_memory(void* mem) {
    if (!mem) return;
    mprotect(mem, page_size, PROT_READ | PROT_WRITE);
    memset(mem, 0, page_size);
    mprotect(mem, page_size, PROT_NONE);
    munmap(mem, page_size);
}

static int rc4_acquire_slot() {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (protected_pool[i].mutex.try_lock()) {
            if (!protected_pool[i].memory) {
                protected_pool[i].memory = rc4_allocate_protected_memory();
                if (!protected_pool[i].memory) {
                    protected_pool[i].mutex.unlock();
                    return -1;
                }
            }
            if (!protected_pool[i].in_use) {
                protected_pool[i].in_use = true;
                return i; // Мьютекс остаётся захваченным на всё время жизни состояния
            }
            protected_pool[i].mutex.unlock();
        }
    }
    return -1;
}

static void rc4_release_slot(int slot) {
    if (slot < 0 || slot >= MAX_THREADS) return;
    protected_pool[slot].in_use = false;
    protected_pool[slot].mutex.unlock();
}

static void rc4_cleanup_protected_memory() {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (protected_pool[i].mutex.try_lock()) {
            rc4_destroy_protected_memory(protected_pool[i].memory);
            protected_pool[i].memory = nullptr;
            protected_pool[i].in_use = false;
            protected_pool[i].mutex.unlock();
        }
    }
}

extern "C" {

void rc4_init() {
    std::lock_guard<std::mutex> lock(init_mutex);
    if (initialized) return;
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size < sizeof(ProtectedRC4State)) {
        write(STDERR_FILENO, "[RC4 ERROR] Page size too small\n", 32);
        exit(EXIT_FAILURE);
    }
    rc4_install_segv_handler();
    atexit(rc4_cleanup);
    initialized = true;
}

/*
 * Инициализация состояния: KSA + сброс индексов i, j
 * Память защищается PROT_NONE после инициализации
 */
int rc4_state_init(const unsigned char *master_key, size_t master_len, const unsigned char *salt) {
    if (!initialized || !master_key || !salt || master_len > 256) return -1;

    int slot = -1;
    for (int r = 0; r < 10000 && slot == -1; r++) {
        slot = rc4_acquire_slot();
        if (slot == -1) usleep(1000);
    }
    if (slot == -1) return -1;

    void* mem = protected_pool[slot].memory;
    if (mprotect(mem, page_size, PROT_READ | PROT_WRITE) == -1) {
        rc4_release_slot(slot);
        return -1;
    }

    ProtectedRC4State *state = static_cast<ProtectedRC4State*>(mem);

    // Копируем ключ и соль
    memcpy(state->key, master_key, master_len);
    memcpy(state->key + master_len, salt, 16);
    state->key_len = master_len + 16;

    // KSA
    for (int k = 0; k < 256; k++) state->S[k] = k;
    int j = 0;
    for (int k = 0; k < 256; k++) {
        j = (j + state->S[k] + state->key[k % state->key_len]) % 256;
        unsigned char tmp = state->S[k];
        state->S[k] = state->S[j];
        state->S[j] = tmp;
    }

    // Сброс индексов PRGA
    state->i = 0;
    state->j = 0;

    mprotect(mem, page_size, PROT_NONE);
    return slot;
}

/*
 * Обработка чанка: PRGA с сохранением i, j в защищённой памяти
 */
int rc4_state_update(int slot, const unsigned char *input, unsigned char *output, size_t length) {
    if (slot < 0 || slot >= MAX_THREADS || !protected_pool[slot].in_use) return -1;

    void* mem = protected_pool[slot].memory;
    if (mprotect(mem, page_size, PROT_READ | PROT_WRITE) == -1) return -1;

    ProtectedRC4State *state = static_cast<ProtectedRC4State*>(mem);
    int i = state->i;
    int j = state->j;

    for (size_t k = 0; k < length; k++) {
        i = (i + 1) % 256;
        j = (j + state->S[i]) % 256;
        unsigned char tmp = state->S[i];
        state->S[i] = state->S[j];
        state->S[j] = tmp;
        unsigned char keystream = state->S[(state->S[i] + state->S[j]) % 256];
        output[k] = input[k] ^ keystream;
    }

    state->i = i;
    state->j = j;

    mprotect(mem, page_size, PROT_NONE);
    return 0;
}

/*
 * Освобождение состояния: безопасное затирание и закрытие слота
 */
void rc4_state_free(int slot) {
    if (slot < 0 || slot >= MAX_THREADS || !protected_pool[slot].in_use) return;

    void* mem = protected_pool[slot].memory;
    mprotect(mem, page_size, PROT_READ | PROT_WRITE);
    memset(mem, 0, page_size);
    mprotect(mem, page_size, PROT_NONE);
    rc4_release_slot(slot);
}

/*
 * Legacy wrapper для однократного вызова
 */
void rc4_crypt(const unsigned char *master_key, size_t master_len,
               const unsigned char *salt, const unsigned char *input,
               unsigned char *output, size_t length) {
    int slot = rc4_state_init(master_key, master_len, salt);
    if (slot < 0) {
        write(STDERR_FILENO, "[RC4 ERROR] state_init failed\n", 30);
        exit(EXIT_FAILURE);
    }
    rc4_state_update(slot, input, output, length);
    rc4_state_free(slot);
}

void rc4_cleanup() {
    std::lock_guard<std::mutex> lock(init_mutex);
    if (!initialized) return;
    rc4_cleanup_protected_memory();
    initialized = false;
}

} // extern "C"