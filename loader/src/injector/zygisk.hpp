#pragma once

#include <jni.h>

extern void *start_addr;
extern size_t block_size;

void hook_functions();

void clean_trace(const char *path, void **module_addrs, size_t module_addrs_length, size_t load, size_t unload, bool spoof_maps);

extern "C" void send_seccomp_event();
