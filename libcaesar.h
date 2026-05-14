#pragma once

extern "C" {

void init_secure_crypto();

void set_key(char k);

void caesar(void* src, void* dst, int len);

void cleanup_secure_crypto();

void emergency_cleanup();

}
