#include "misc.h"
#include "daemon.h"
#include "logging.h"
#include "solist.h"
#include "zygisk.hpp"

void *start_addr = NULL;
size_t block_size = 0;

extern "C" [[gnu::visibility("default")]]
void entry(void *addr, size_t size) {
    LOGD("Zygisk library injected, version %s", ZKSU_VERSION);

    start_addr = addr;
    block_size = size;

    if (!rezygiskd_ping()) {
        LOGE("Zygisk daemon is not running");

        return;
    }

    LOGD("start plt hooking");
    hook_functions();

    solist_drop_so_path(addr);
    solist_reset_counters(1);

    struct kernel_version version = parse_kversion();
    if (version.major > 3 || (version.major == 3 && version.minor >= 8)) {
        LOGD("Supported kernel version %d.%d.%d, sending seccomp event", version.major, version.minor, version.patch);

        send_seccomp_event();
    }

    LOGD("Zygisk library execution done, addr: %p, size: %zu", addr, size);
}
