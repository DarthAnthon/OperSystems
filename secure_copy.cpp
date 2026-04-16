#include <iostream>
#include <fstream>
#include <queue>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

extern "C" { void set_key(char k); void caesar(void* src, void* dst, int len); }

#define WORKERS_COUNT 4

double now_sec() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

struct Shared {
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    std::queue<std::string> files;
    std::vector<double> times;
    std::ofstream log;
    std::string outdir;
    int done = 0;
    bool stop = false;
    Shared() {}
    ~Shared() { pthread_mutex_destroy(&mtx); pthread_cond_destroy(&cond); }
};

void lock_mtx(pthread_mutex_t* m, int tid) {
    timespec ts;
    while (pthread_mutex_timedlock(m, (clock_gettime(CLOCK_REALTIME, &ts), ts.tv_sec += 5, &ts))) {
        std::cerr << "Поток " << tid << " ждёт мьютекс >5с\n";
    }
}

bool proc_file(const std::string& in, const std::string& outdir) {
    std::ifstream f(in, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    size_t sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> buf(sz);
    if (sz && !f.read(buf.data(), sz)) return false;
    f.close();
    if (sz) caesar(buf.data(), buf.data(), sz);
    std::string out = outdir + "/" + (in.find_last_of("/\\") == std::string::npos ? in : in.substr(in.find_last_of("/\\")+1));
    std::ofstream o(out, std::ios::binary);
    return o.write(buf.data(), sz).good();
}

void* worker(void* arg) {
    Shared* sh = (Shared*)((void**)arg)[0];
    int tid = *(int*)((void**)arg)[1];
    while (true) {
        lock_mtx(&sh->mtx, tid);
        while (sh->files.empty() && !sh->stop) pthread_cond_wait(&sh->cond, &sh->mtx);
        if (sh->files.empty() && sh->stop) { pthread_mutex_unlock(&sh->mtx); break; }
        std::string f = sh->files.front(); sh->files.pop();
        pthread_mutex_unlock(&sh->mtx);

        double t0 = now_sec();
        bool ok = proc_file(f, sh->outdir);
        double dt = now_sec() - t0;

        char tbuf[20];
        time_t now = time(nullptr);
        strftime(tbuf, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));

        lock_mtx(&sh->mtx, tid);
        sh->log << "[" << tbuf << "] [thread " << tid << "] " << f << " " << (ok ? "SUCCESS" : "ERROR") << " " << dt << "s\n";
        if (ok) { sh->done++; sh->times.push_back(dt); }
        pthread_mutex_unlock(&sh->mtx);
    }
    return nullptr;
}

struct Stats { double total, avg; int files; };

Stats sequential(const std::vector<std::string>& files, const std::string& outdir, std::ofstream& log) {
    double t0 = now_sec();
    int okcnt = 0;
    double total = 0;
    for (const auto& f : files) {
        double ft0 = now_sec();
        bool ok = proc_file(f, outdir);
        double ft = now_sec() - ft0;
        char tbuf[20]; time_t now = time(nullptr); strftime(tbuf, 20, "%Y-%m-%d %H:%M:%S", localtime(&now));
        log << "[" << tbuf << "] [sequential] " << f << " " << (ok ? "SUCCESS" : "ERROR") << " " << ft << "s\n";
        if (ok) { okcnt++;}
    }
    total = now_sec() - t0;
    return {total, okcnt ? total / okcnt : 0.0, okcnt};
}

Stats parallel(const std::vector<std::string>& files, const std::string& outdir, std::ofstream& log) {
    Shared sh;
    sh.outdir = outdir;
    sh.log.swap(log);
    for (const auto& f : files) sh.files.push(f);
    pthread_t th[WORKERS_COUNT];
    void* args[WORKERS_COUNT][2];
    for (int i = 0; i < WORKERS_COUNT; ++i) {
        args[i][0] = &sh; args[i][1] = new int(i);
        pthread_create(&th[i], nullptr, worker, args[i]);
    }
    double t0 = now_sec();
    pthread_mutex_lock(&sh.mtx); //захват мьютекса, для получения доступа к списку файлов и флагу завершения
    while (!sh.files.empty()) {
        pthread_cond_broadcast(&sh.cond); //пробуждение всех потоков, находящихся в ожидании
        pthread_mutex_unlock(&sh.mtx); //особождение мьютекса для потоков
        usleep(1000); //короткая пауза, чтобы не загружать процессор пустым циклом
        pthread_mutex_lock(&sh.mtx); //очередной захват мьютекса для получения главным потоком доступа к списку файлов и флагу
    }
    sh.stop = true; //флаг завершения работы
    pthread_cond_broadcast(&sh.cond); //пробуждение потоков, чтобы они увидели флаг
    pthread_mutex_unlock(&sh.mtx); //финальное освобождение мьютекса
    for (int i = 0; i < WORKERS_COUNT; ++i) {
        pthread_join(th[i], nullptr);
        delete (int*)args[i][1];
    }
    log.swap(sh.log);
    double total = now_sec() - t0;
    return {total, sh.times.empty() ? 0.0 : total / sh.times.size(), sh.done};
}

void print_stats(const char* name, const Stats& s) {
    std::cout << "=== " << name << " ===\nФайлов: " << s.files << "\nОбщее время: " << s.total << " с\nСреднее: " << s.avg << " с\n\n";
}

int main(int argc, char** argv) {
    if (argc < 4) { std::cerr << "Usage: " << argv[0] << " [--mode=seq|par] файлы... outdir key\n"; return 1; }
    int fstart = 1;
    enum { AUTO, SEQ, PAR } mode = AUTO;
    if (argc > 1 && strncmp(argv[1], "--mode=", 7) == 0) {
        std::string m = argv[1] + 7;
        mode = (m == "sequential") ? SEQ : (m == "parallel") ? PAR : AUTO;
        if (mode == AUTO) { std::cerr << "Неизвестный режим\n"; return 1; }
        fstart = 2;
    }
    std::vector<std::string> files;
    for (int i = fstart; i < argc - 2; ++i) files.push_back(argv[i]);
    std::string outdir = argv[argc - 2];
    char key = argv[argc - 1][0];
    set_key(key);
    mkdir(outdir.c_str(), 0755);
    std::ofstream log("log.txt", std::ios::app);
    if (!log) return 1;
    if (mode == AUTO) mode = (files.size() < 5) ? SEQ : PAR;
    Stats res = (mode == SEQ) ? sequential(files, outdir, log) : parallel(files, outdir, log);
    print_stats((mode == SEQ) ? "ПОСЛЕДОВАТЕЛЬНЫЙ" : "ПАРАЛЛЕЛЬНЫЙ", res);
    log.close();
    return 0;
}