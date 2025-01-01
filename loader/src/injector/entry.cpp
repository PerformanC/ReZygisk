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
    zygiskd::Init(path);

    if (!zygiskd::PingHeartbeat()) {
        LOGE("Zygisk daemon is not running");
        return;
    }

#ifdef NDEBUG
    logging::setfd(zygiskd::RequestLogcatFd());
#endif

    LOGD("start plt hooking");
    hook_functions();
    clean_trace(path, 1, 0, false);
}
