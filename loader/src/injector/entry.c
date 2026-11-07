#include "daemon.h"
#include "logging.h"
#include "misc.h"

#include <sys/stat.h>

#include "hook.h"
#include "ptrace_clear.h"

__attribute__((visibility("default")))
void entry(void *addr, size_t size, int tango_flag) {
  LOGD("ReZygisk%s library injected, version %s", tango_flag ? " [TANGO]" : "", ZKSU_VERSION);

  start_addr = addr;
  block_size = size;

  struct stat self_stat;
  if (stat("/data/adb/modules/rezygisk/lib" LP_SELECT("", "64") "/libzygisk.so", &self_stat) != 0) {
    PLOGE("Failed to stat self library");

    return;
  }

  self_dev = self_stat.st_dev;
  self_ino = self_stat.st_ino;

  LOGD("start plt hooking");
  hook_functions();

  struct kernel_version version = parse_kversion();
  if (version.major > 3 || (version.major == 3 && version.minor >= 8)) {
    LOGD("Supported kernel version %d.%d.%d, sending seccomp event", version.major, version.minor, version.patch);

    perform_ptrace_message_clear();
  }

  if (!rezygiskd_zygote_injected()) {
    LOGE("ReZygiskd is not running");

    return;
  }

  LOGD("Zygisk library execution done, addr: %p, size: %zu", addr, size);
}
