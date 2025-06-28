#include "daemon.h"
#include "logging.h"
#include "zygisk.hpp"

using namespace std;

void *start_addr = nullptr;
size_t block_size = 0;

extern "C" [[gnu::visibility("default")]]
void entry(void* addr, size_t size, const char* path) {
    LOGD("Zygisk library injected, version %s", ZKSU_VERSION);

    start_addr = addr;
    block_size = size;

    if (!rezygiskd_ping()) {
        LOGE("Zygisk daemon is not running");

        return;
    }

    LOGD("start plt hooking");
    hook_functions();

    void *module_addrs[1] = { addr };
    clean_trace(path, module_addrs, 1, 1, 0, false);
    send_seccomp_event();
}
