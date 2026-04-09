// secure_copy.cpp - сжатая версия
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

struct Shared {
    pthread_mutex_t mtx;
    std::queue<std::string> files;
    int copied;
    std::string outdir;
    char key;
    std::ofstream log;
    Shared() : mtx(PTHREAD_MUTEX_INITIALIZER), copied(0) {}
    ~Shared() { pthread_mutex_destroy(&mtx); }
};

// Безопасный захват с таймаутом 5 сек
void safe_lock(pthread_mutex_t* m, int tid) {
    timespec ts;
    while (true) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;
        if (pthread_mutex_timedlock(m, &ts) == 0) return;
        std::cerr << "Возможная взаимоблокировка: поток " << tid << " ждёт мьютекс >5с\n";
    }
}

bool process(const std::string& in, const std::string& outdir, char key) {
    std::ifstream f(in, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    size_t sz = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> buf(sz);
    if (sz && !f.read(buf.data(), sz)) return false;
    f.close();
    if (sz) caesar(buf.data(), buf.data(), sz);
    std::string out = outdir + "/" + (in.find_last_of("/\\")+1 ? in.substr(in.find_last_of("/\\")+1) : in);
    std::ofstream o(out, std::ios::binary);
    if (!o) return false;
    if (sz) o.write(buf.data(), sz);
    return true;
}

void* worker(void* arg) {
    auto* sh = (Shared*)((void**)arg)[0];
    int tid = *(int*)((void**)arg)[1];
    while (true) {
        safe_lock(&sh->mtx, tid);
        if (sh->files.empty()) { pthread_mutex_unlock(&sh->mtx); break; }
        std::string f = sh->files.front(); sh->files.pop();
        pthread_mutex_unlock(&sh->mtx);
        auto start = std::chrono::steady_clock::now();
        bool ok = process(f, sh->outdir, sh->key);
        double dur = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        char tbuf[20];
        time_t now = time(nullptr);
        strftime(tbuf,20,"%Y-%m-%d %H:%M:%S",localtime(&now));
        safe_lock(&sh->mtx, tid);
        sh->log << "[" << tbuf << "] [thread " << tid << "] " << f << " " << (ok?"SUCCESS":"ERROR") << " " << dur << "s\n";
        if (ok) sh->copied++;
        pthread_mutex_unlock(&sh->mtx);
    }
    return nullptr;
}

int main(int argc, char** argv) {
    if (argc < 4) { std::cerr << "Usage: " << argv[0] << " file... outdir key\n"; return 1; }
    Shared sh;
    sh.outdir = argv[argc-2];
    sh.key = argv[argc-1][0];
    set_key(sh.key);
    mkdir(sh.outdir.c_str(), 0755);
    sh.log.open("log.txt", std::ios::app);
    for (int i=1; i<argc-2; ++i) sh.files.push(argv[i]);
    pthread_t threads[3];
    void* args[3][2];
    for (int i=0; i<3; ++i) {
        args[i][0] = &sh;
        args[i][1] = new int(i);
        pthread_create(&threads[i], nullptr, worker, args[i]);
    }
    for (int i=0; i<3; ++i) { pthread_join(threads[i], nullptr); delete (int*)args[i][1]; }
    sh.log.close();
    std::cout << "Скопировано файлов: " << sh.copied << " из " << (argc-3) << "\n";
    return 0;
}