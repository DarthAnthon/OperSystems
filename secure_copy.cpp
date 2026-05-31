#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <queue>
#include <random>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" {
    void rc4_init();
    int rc4_state_init(const unsigned char *master_key, size_t master_len, const unsigned char *salt);
    int rc4_state_update(int slot, const unsigned char *input, unsigned char *output, size_t length);
    void rc4_state_free(int slot);
}

#define MAX_WORKERS 5
#define CHUNK_SIZE (1024 * 1024)  // 1 МБ

/* ==========================================================================
   Вспомогательные функции безопасности
   ========================================================================== */
static void secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
    while (len--) *p++ = 0;
}

static void wipe_string(std::string &s) {
    if (!s.empty()) {
        secure_zero(&s[0], s.size());
        s.clear();
    }
}

/* ==========================================================================
   Структуры данных
   ========================================================================== */
struct ProtectedMasterKey {
    unsigned char data[256];
    size_t length;

    ProtectedMasterKey() : length(0) { secure_zero(data, sizeof(data)); }
    ~ProtectedMasterKey() { secure_zero(data, sizeof(data)); length = 0; }

    bool set_from_string(std::string &key_str) {
        if (key_str.length() > 256) {
            std::cerr << "[ERROR] Key too long (max 256 bytes)" << std::endl;
            return false;
        }
        length = key_str.length();
        memcpy(data, key_str.data(), length);
        wipe_string(key_str);
        return true;
    }

    const unsigned char *get_data() const { return data; }
    size_t get_length() const { return length; }
};

struct FileRecord {
    uint32_t file_length;
    uint32_t name_length;
    unsigned char salt[16];
    std::string name;
    std::streampos content_pos; // Позиция начала содержимого в образе
};

struct AddTask {
    std::string src_path;
    std::string name;
    int img_fd;
    off_t content_offset;
    uint32_t file_size;
    unsigned char salt[16];
};

struct SharedAddData {
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    std::queue<AddTask> tasks;
    const ProtectedMasterKey &protected_key;
    bool stop = false;
    int errors = 0;
    int completed = 0;

    SharedAddData(const ProtectedMasterKey &key) : protected_key(key) {}
    ~SharedAddData() {
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&cond);
    }
};

/* ==========================================================================
   Утилиты
   ========================================================================== */
static ssize_t pwrite_all(int fd, const void *buf, size_t count, off_t offset) {
    size_t written = 0;
    while (written < count) {
        ssize_t res = pwrite(fd, static_cast<const char *>(buf) + written,
                             count - written, offset + static_cast<off_t>(written));
        if (res <= 0) return res;
        written += res;
    }
    return static_cast<ssize_t>(written);
}

void generate_salt(unsigned char *salt) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (int i = 0; i < 16; i++) salt[i] = static_cast<unsigned char>(dis(gen));
}

std::string normalize_path(const std::string &path) {
    std::string result = path;
    for (auto &c : result) if (c == '\\') c = '/';
    while (!result.empty() && result.back() == '/') result.pop_back();
    size_t pos = 0;
    while ((pos = result.find("//", pos)) != std::string::npos) result.erase(pos, 1);
    return result;
}

void collect_files_recursive(const std::string &path, const std::string &base_path,
                             std::vector<std::pair<std::string, std::string>> &files) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        std::cerr << "[WARNING] Cannot access: " << path << std::endl;
        return;
    }
    if (S_ISREG(st.st_mode)) {
        std::string rel = path;
        if (path.find(base_path) == 0) {
            rel = path.substr(base_path.length());
            if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
        }
        files.push_back({path, rel});
    } else if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path.c_str());
        if (!dir) {
            std::cerr << "[WARNING] Cannot open directory: " << path << std::endl;
            return;
        }
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string n = entry->d_name;
            if (n == "." || n == "..") continue;
            collect_files_recursive(path + "/" + n, base_path, files);
        }
        closedir(dir);
    }
}

bool read_image_headers(const std::string &image_path, std::vector<FileRecord> &records) {
    std::ifstream img(image_path, std::ios::binary);
    if (!img) return false;

    while (img.peek() != EOF) {
        FileRecord rec;
        img.read(reinterpret_cast<char *>(&rec.file_length), 4);
        if (!img) break;
        img.read(reinterpret_cast<char *>(&rec.name_length), 4);
        if (!img) break;
        img.read(reinterpret_cast<char *>(rec.salt), 16);
        if (!img) break;

        rec.name.resize(rec.name_length);
        img.read(&rec.name[0], rec.name_length);
        if (!img) break;

        rec.content_pos = img.tellg();
        if (rec.file_length > 0) {
            img.seekg(static_cast<std::streamoff>(rec.file_length), std::ios::cur);
            if (!img) break;
        }
        records.push_back(rec);
    }
    return true;
}

/* ==========================================================================
   Рабочий поток: шифрование и прямая запись в образ
   ========================================================================== */
void *add_worker(void *arg) {
    SharedAddData *sh = static_cast<SharedAddData *>(arg);

    while (true) {
        pthread_mutex_lock(&sh->mtx);
        while (sh->tasks.empty() && !sh->stop) {
            pthread_cond_wait(&sh->cond, &sh->mtx);
        }
        if (sh->tasks.empty() && sh->stop) {
            pthread_mutex_unlock(&sh->mtx);
            break;
        }

        AddTask task = sh->tasks.front();
        sh->tasks.pop();
        pthread_mutex_unlock(&sh->mtx);

        std::ifstream in(task.src_path, std::ios::binary);
        if (!in) {
            pthread_mutex_lock(&sh->mtx);
            sh->errors++;
            std::cerr << "[ERROR] Cannot open: " << task.src_path << std::endl;
            pthread_mutex_unlock(&sh->mtx);
            continue;
        }

        // Инициализация состояния RC4 (KSA выполняется один раз на файл)
        int slot = rc4_state_init(sh->protected_key.get_data(),
                                  sh->protected_key.get_length(),
                                  task.salt);
        if (slot < 0) {
            pthread_mutex_lock(&sh->mtx);
            sh->errors++;
            std::cerr << "[ERROR] RC4 init failed for: " << task.name << std::endl;
            pthread_mutex_unlock(&sh->mtx);
            continue;
        }

        std::vector<unsigned char> plain(CHUNK_SIZE);
        std::vector<unsigned char> enc(CHUNK_SIZE);
        uint32_t processed = 0;
        bool ok = true;

        while (processed < task.file_size && ok) {
            uint32_t to_read = std::min<uint32_t>(CHUNK_SIZE, task.file_size - processed);
            in.read(reinterpret_cast<char *>(plain.data()), to_read);
            if (in.gcount() != static_cast<std::streamsize>(to_read)) {
                std::cerr << "[ERROR] Read failed: " << task.src_path << std::endl;
                ok = false;
                break;
            }

            // Потоковое обновление RC4 (индексы i/j сохраняются в защищённой памяти)
            if (rc4_state_update(slot, plain.data(), enc.data(), to_read) != 0) {
                std::cerr << "[ERROR] RC4 update failed: " << task.name << std::endl;
                ok = false;
                break;
            }

            if (pwrite_all(task.img_fd, enc.data(), to_read,
                           task.content_offset + processed) != static_cast<ssize_t>(to_read)) {
                std::cerr << "[ERROR] Write failed: " << task.name << std::endl;
                ok = false;
                break;
            }
            processed += to_read;
        }

        // Безопасное уничтожение состояния шифра
        rc4_state_free(slot);

        pthread_mutex_lock(&sh->mtx);
        if (ok) {
            sh->completed++;
            std::cout << "[OK] Encrypted: " << task.name << " (" << task.file_size << " bytes)" << std::endl;
        } else {
            sh->errors++;
        }
        pthread_mutex_unlock(&sh->mtx);
    }
    return nullptr;
}

/* ==========================================================================
   Команда: -add
   ========================================================================== */
int cmd_add(const std::string &image_path, const ProtectedMasterKey &protected_key,
            const std::vector<std::string> &paths) {
    std::vector<std::pair<std::string, std::string>> all_files;
    for (const auto &p : paths) {
        struct stat st;
        if (stat(p.c_str(), &st) != 0) {
            std::cerr << "[ERROR] Cannot access: " << p << std::endl;
            continue;
        }
        if (S_ISREG(st.st_mode)) {
            all_files.push_back({p, normalize_path(p)});
        } else if (S_ISDIR(st.st_mode)) {
            collect_files_recursive(p, p, all_files);
        }
    }
    if (all_files.empty()) {
        std::cerr << "[ERROR] No files to add" << std::endl;
        return 1;
    }

    std::cout << "[INFO] Found " << all_files.size() << " file(s) to add" << std::endl;

    int img_fd = open(image_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (img_fd == -1) {
        std::cerr << "[ERROR] Cannot open image: " << image_path << " (" << strerror(errno) << ")" << std::endl;
        return 1;
    }

    off_t current_pos = lseek(img_fd, 0, SEEK_END);
    if (current_pos == -1) {
        std::cerr << "[ERROR] lseek failed" << std::endl;
        close(img_fd);
        return 1;
    }

    std::vector<AddTask> tasks;
    tasks.reserve(all_files.size());

    for (const auto &f : all_files) {
        struct stat st;
        if (stat(f.first.c_str(), &st) != 0) continue;
        if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) > UINT32_MAX) {
            std::cerr << "[WARNING] Skipping too large file: " << f.first << std::endl;
            continue;
        }

        AddTask task;
        task.src_path = f.first;
        task.name = f.second;
        task.img_fd = img_fd;
        task.file_size = static_cast<uint32_t>(st.st_size);
        generate_salt(task.salt);

        uint32_t nlen = static_cast<uint32_t>(task.name.size());
        if (write(img_fd, &task.file_size, 4) != 4 ||
            write(img_fd, &nlen, 4) != 4 ||
            write(img_fd, task.salt, 16) != 16 ||
            write(img_fd, task.name.c_str(), nlen) != static_cast<ssize_t>(nlen)) {
            std::cerr << "[ERROR] Failed to write header for: " << task.name << std::endl;
            continue;
        }

        task.content_offset = lseek(img_fd, 0, SEEK_CUR);
        lseek(img_fd, task.file_size, SEEK_CUR);
        tasks.push_back(task);
    }

    if (tasks.empty()) {
        std::cerr << "[ERROR] No valid files to process" << std::endl;
        close(img_fd);
        return 1;
    }

    off_t final_size = lseek(img_fd, 0, SEEK_CUR);
    if (ftruncate(img_fd, final_size) == -1) {
        std::cerr << "[WARNING] ftruncate failed: " << strerror(errno) << std::endl;
    }

    SharedAddData shared(protected_key);
    for (auto &t : tasks) shared.tasks.push(t);

    int num_workers = std::min(MAX_WORKERS, static_cast<int>(tasks.size()));
    pthread_t threads[MAX_WORKERS];
    std::cout << "[INFO] Starting " << num_workers << " worker thread(s)" << std::endl;

    for (int i = 0; i < num_workers; i++) {
        pthread_create(&threads[i], nullptr, add_worker, &shared);
    }

    pthread_mutex_lock(&shared.mtx);
    while (!shared.tasks.empty()) {
        pthread_cond_broadcast(&shared.cond);
        pthread_mutex_unlock(&shared.mtx);
        usleep(10000);
        pthread_mutex_lock(&shared.mtx);
    }
    shared.stop = true;
    pthread_cond_broadcast(&shared.cond);
    pthread_mutex_unlock(&shared.mtx);

    for (int i = 0; i < num_workers; i++) pthread_join(threads[i], nullptr);
    close(img_fd);

    if (shared.errors > 0) {
        std::cerr << "[WARNING] " << shared.errors << " file(s) failed" << std::endl;
    }
    std::cout << "[SUCCESS] Added " << shared.completed << " file(s) to image" << std::endl;
    return (shared.completed > 0) ? 0 : 1;
}

/* ==========================================================================
   Команда: -list
   ========================================================================== */
int cmd_list(const std::string &image_path) {
    std::vector<FileRecord> records;
    if (!read_image_headers(image_path, records)) {
        std::cerr << "[ERROR] Cannot read image: " << image_path << std::endl;
        return 1;
    }
    if (records.empty()) {
        std::cout << "[INFO] Image is empty" << std::endl;
        return 0;
    }
    std::sort(records.begin(), records.end(),
              [](const FileRecord &a, const FileRecord &b) { return a.name < b.name; });

    std::cout << "Files in image: " << records.size() << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    for (const auto &r : records) {
        std::cout << r.name << " (" << r.file_length << " bytes)" << std::endl;
    }
    return 0;
}

/* ==========================================================================
   Команда: -get (включая потоковое расшифрование)
   ========================================================================== */
bool decrypt_record_to_file(const std::string &image_path, const FileRecord &rec,
                            const ProtectedMasterKey &key, const std::string &output_path) {
    std::ifstream img(image_path, std::ios::binary);
    if (!img) {
        std::cerr << "[ERROR] Cannot open image: " << image_path << std::endl;
        return false;
    }
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "[ERROR] Cannot create output: " << output_path << std::endl;
        return false;
    }

    img.seekg(rec.content_pos);
    if (!img) {
        std::cerr << "[ERROR] Cannot seek to content in image" << std::endl;
        return false;
    }

    // Инициализация состояния RC4 для расшифровки
    int slot = rc4_state_init(key.get_data(), key.get_length(), rec.salt);
    if (slot < 0) {
        std::cerr << "[ERROR] RC4 init failed for decryption" << std::endl;
        return false;
    }

    std::vector<unsigned char> enc(CHUNK_SIZE);
    std::vector<unsigned char> plain(CHUNK_SIZE);
    uint32_t processed = 0;
    bool ok = true;

    while (processed < rec.file_length && ok) {
        uint32_t to_read = std::min<uint32_t>(CHUNK_SIZE, rec.file_length - processed);
        img.read(reinterpret_cast<char *>(enc.data()), to_read);
        if (img.gcount() != static_cast<std::streamsize>(to_read)) {
            std::cerr << "[ERROR] Read from image failed" << std::endl;
            ok = false;
            break;
        }

        if (rc4_state_update(slot, enc.data(), plain.data(), to_read) != 0) {
            std::cerr << "[ERROR] RC4 decrypt update failed" << std::endl;
            ok = false;
            break;
        }

        out.write(reinterpret_cast<const char *>(plain.data()), to_read);
        if (!out) {
            std::cerr << "[ERROR] Write output failed" << std::endl;
            ok = false;
            break;
        }
        processed += to_read;
    }

    rc4_state_free(slot);
    return ok;
}

int cmd_get(const std::string &image_path, const ProtectedMasterKey &protected_key,
            const std::string &file_name, const std::string &output_path) {
    std::vector<FileRecord> records;
    if (!read_image_headers(image_path, records)) {
        std::cerr << "[ERROR] Cannot read image: " << image_path << std::endl;
        return 1;
    }
    std::string search = normalize_path(file_name);
    const FileRecord *found = nullptr;
    for (const auto &r : records) {
        if (r.name == search) { found = &r; break; }
    }
    if (!found) {
        std::cerr << "[ERROR] File not found in image: " << file_name << std::endl;
        return 1;
    }
    if (!decrypt_record_to_file(image_path, *found, protected_key, output_path)) return 1;
    std::cout << "[SUCCESS] Decrypted file saved to: " << output_path << std::endl;
    return 0;
}

/* ==========================================================================
   Main & Usage
   ========================================================================== */
void print_usage(const char *prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " -add -key <key> -image <image> <files...>\n"
              << "  " << prog << " -list -image <image>\n"
              << "  " << prog << " -get -image <image> -key <key> -out <output> <filename>\n";
}

int main(int argc, char **argv) {
    rc4_init();
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string cmd = argv[1];
    if (cmd == "-add") {
        std::string key_str, image;
        std::vector<std::string> files;
        int i = 2;
        while (i < argc) {
            std::string a = argv[i];
            if (a == "-key" && i + 1 < argc) {
                key_str = argv[++i];
                if (argv[i]) secure_zero(argv[i], strlen(argv[i]));
                i++;
            } else if (a == "-image" && i + 1 < argc) {
                image = argv[++i]; i++;
            } else if (a[0] == '-') {
                std::cerr << "[ERROR] Unknown flag '" << a << "'\n"; return 1;
            } else { files.push_back(a); i++; }
        }
        if (key_str.empty() || image.empty() || files.empty()) {
            std::cerr << "[ERROR] Missing required arguments\n"; print_usage(argv[0]); return 1;
        }
        ProtectedMasterKey pk;
        if (!pk.set_from_string(key_str)) return 1;
        return cmd_add(image, pk, files);
    }
    if (cmd == "-list") {
        std::string image;
        int i = 2;
        while (i < argc) {
            std::string a = argv[i];
            if (a == "-image" && i + 1 < argc) { image = argv[++i]; i++; }
            else if (a[0] == '-') { std::cerr << "[ERROR] Unknown flag '" << a << "'\n"; return 1; }
            else { std::cerr << "[ERROR] Unexpected argument '" << a << "'\n"; return 1; }
        }
        if (image.empty()) { std::cerr << "[ERROR] Missing -image\n"; return 1; }
        return cmd_list(image);
    }
    if (cmd == "-get") {
        std::string key_str, image, out, fname;
        int i = 2;
        while (i < argc) {
            std::string a = argv[i];
            if (a == "-key" && i + 1 < argc) {
                key_str = argv[++i];
                if (argv[i]) secure_zero(argv[i], strlen(argv[i]));
                i++;
            } else if (a == "-image" && i + 1 < argc) { image = argv[++i]; i++; }
            else if (a == "-out" && i + 1 < argc) { out = argv[++i]; i++; }
            else if (a[0] == '-') { std::cerr << "[ERROR] Unknown flag '" << a << "'\n"; return 1; }
            else {
                if (fname.empty()) fname = a;
                else { std::cerr << "[ERROR] Too many arguments\n"; return 1; }
                i++;
            }
        }
        if (key_str.empty() || image.empty() || out.empty() || fname.empty()) {
            std::cerr << "[ERROR] Missing required arguments\n"; print_usage(argv[0]); return 1;
        }
        ProtectedMasterKey pk;
        if (!pk.set_from_string(key_str)) return 1;
        return cmd_get(image, pk, fname, out);
    }
    std::cerr << "[ERROR] Unknown command: " << cmd << std::endl;
    print_usage(argv[0]);
    return 1;
}