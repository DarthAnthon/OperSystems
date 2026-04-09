#include <iostream>
#include <fstream>
#include <queue>
#include <pthread.h>
#include <csignal>
#include <ctime>

// Функции из библиотеки libcaesar.so
extern "C" {
    void set_key(char k);
    void caesar(void* src, void* dst, int len);
}

// Глобальный флаг для обработки SIGINT
volatile sig_atomic_t keep_running = 1;

// Контекст, передаваемый потокам
struct ThreadContext {
    std::ifstream* in;
    std::ofstream* out;
    pthread_mutex_t mutex;
    pthread_cond_t cond_prod;   // сигнал для producer (появился свободный блок)
    pthread_cond_t cond_cons;   // сигнал для consumer (появился заполненный блок)

    static const int NUM_BLOCKS = 4;
    static const int BLOCK_SIZE = 4096;

    struct Block {
        char data[BLOCK_SIZE];
        size_t size;
    } blocks[NUM_BLOCKS];

    std::queue<Block*> free_blocks;    // очередь свободных блоков
    std::queue<Block*> filled_blocks;  // очередь заполненных блоков
    bool producer_finished;             // producer достиг конца файла

    ThreadContext() : in(nullptr), out(nullptr), producer_finished(false) {
        pthread_mutex_init(&mutex, nullptr);
        pthread_cond_init(&cond_prod, nullptr);
        pthread_cond_init(&cond_cons, nullptr);
        for (int i = 0; i < NUM_BLOCKS; ++i)
            free_blocks.push(&blocks[i]);
    }

    ~ThreadContext() {
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond_prod);
        pthread_cond_destroy(&cond_cons);
    }
};

// Поток-производитель: читает, шифрует, передаёт данные
void* producer_thread(void* arg) {
    ThreadContext* ctx = static_cast<ThreadContext*>(arg);

    while (keep_running) {
        // 1. Получить свободный блок
        pthread_mutex_lock(&ctx->mutex);
        while (ctx->free_blocks.empty() && keep_running) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000; // 100 мс
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&ctx->cond_prod, &ctx->mutex, &ts);
        }
        if (!keep_running) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        ThreadContext::Block* blk = ctx->free_blocks.front();
        ctx->free_blocks.pop();
        pthread_mutex_unlock(&ctx->mutex);

        // 2. Чтение из входного файла
        ctx->in->read(blk->data, ctx->BLOCK_SIZE);
        size_t bytes = ctx->in->gcount();

        // Проверка на ошибку чтения или конец файла
        if (bytes == 0) {
            if (ctx->in->eof()) {
                // Нормальный конец файла
                pthread_mutex_lock(&ctx->mutex);
                ctx->free_blocks.push(blk);          // вернуть неиспользованный блок
                ctx->producer_finished = true;
                pthread_cond_signal(&ctx->cond_cons); // разбудить consumer
                pthread_mutex_unlock(&ctx->mutex);
                break;
            } else {
                // Ошибка чтения
                std::cerr << "Error reading input file\n";
                keep_running = 0;
                pthread_mutex_lock(&ctx->mutex);
                ctx->free_blocks.push(blk);
                pthread_cond_broadcast(&ctx->cond_prod);
                pthread_cond_broadcast(&ctx->cond_cons);
                pthread_mutex_unlock(&ctx->mutex);
                break;
            }
        }

        // 3. Шифрование прочитанного блока
        caesar(blk->data, blk->data, bytes);
        blk->size = bytes;

        // 4. Передать блок потребителю
        pthread_mutex_lock(&ctx->mutex);
        ctx->filled_blocks.push(blk);
        pthread_cond_signal(&ctx->cond_cons);
        pthread_mutex_unlock(&ctx->mutex);
    }
    return nullptr;
}

// Поток-потребитель: получает зашифрованные данные и пишет в выходной файл
void* consumer_thread(void* arg) {
    ThreadContext* ctx = static_cast<ThreadContext*>(arg);

    while (keep_running) {
        pthread_mutex_lock(&ctx->mutex);
        while (ctx->filled_blocks.empty() && !ctx->producer_finished && keep_running) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&ctx->cond_cons, &ctx->mutex, &ts);
        }
        if (!keep_running) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }

        if (!ctx->filled_blocks.empty()) {
            ThreadContext::Block* blk = ctx->filled_blocks.front();
            ctx->filled_blocks.pop();
            pthread_mutex_unlock(&ctx->mutex);

            // Запись в выходной файл
            ctx->out->write(blk->data, blk->size);
            if (!ctx->out->good()) {
                std::cerr << "Error writing to output file\n";
                keep_running = 0;
                pthread_mutex_lock(&ctx->mutex);
                ctx->free_blocks.push(blk);          // вернуть блок
                pthread_cond_broadcast(&ctx->cond_prod);
                pthread_cond_broadcast(&ctx->cond_cons);
                pthread_mutex_unlock(&ctx->mutex);
                break;
            }

            // Вернуть блок в пул свободных
            pthread_mutex_lock(&ctx->mutex);
            ctx->free_blocks.push(blk);
            pthread_cond_signal(&ctx->cond_prod);
            pthread_mutex_unlock(&ctx->mutex);
        } else {
            // producer_finished и нет данных – выход
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
    }
    return nullptr;
}

// Обработчик сигнала SIGINT
void sigint_handler(int) {
    keep_running = 0;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <input> <output> <key>\n";
        return 1;
    }

    const char* input_file  = argv[1];
    const char* output_file = argv[2];
    char key = argv[3][0];   // первый символ строки ключа

    // Открытие входного файла
    std::ifstream in(input_file, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input file: " << input_file << '\n';
        return 1;
    }

    // Открытие выходного файла
    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file: " << output_file << '\n';
        return 1;
    }

    // Установка ключа шифрования
    set_key(key);

    // Контекст для потоков
    ThreadContext ctx;
    ctx.in = &in;
    ctx.out = &out;

    // Установка обработчика SIGINT
    signal(SIGINT, sigint_handler);

    // Запуск потоков
    pthread_t prod, cons;
    if (pthread_create(&prod, nullptr, producer_thread, &ctx) != 0) {
        std::cerr << "Failed to create producer thread\n";
        return 1;
    }
    if (pthread_create(&cons, nullptr, consumer_thread, &ctx) != 0) {
        std::cerr << "Failed to create consumer thread\n";
        keep_running = 0;
        pthread_cancel(prod);      // принудительно остановить первый поток
        pthread_join(prod, nullptr);
        return 1;
    }

    // Ожидание завершения потоков
    pthread_join(prod, nullptr);
    pthread_join(cons, nullptr);

    // Закрытие файлов
    in.close();
    out.close();

    // Если завершились по SIGINT, вывести сообщение
    if (!keep_running) {
        std::cout << "Операция прервана пользователем\n";
    }

    return 0;
}