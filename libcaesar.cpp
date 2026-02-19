static char key = 0;

extern "C" {

    void set_key(char k) {
        key = k;
    }

    void caesar(void* src, void* dst, int len) {
        char* s = static_cast<char*>(src);
        char* d = static_cast<char*>(dst);
        for (int i = 0; i < len; ++i) {
            d[i] = s[i] ^ key;
        }
    }

}