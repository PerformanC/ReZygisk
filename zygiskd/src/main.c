#include <stdlib.h>
#include <string.h>

#include "root_impl/common.h"
#include "companion.h"
#include "zygiskd.h"

#include "utils.h"

int main(int argc, char *argv[]) {
  LOGI("Welcome to ReZygiskd%s", LP_SELECT("32", "64"));

  if (argc > 1) {
    if (strcmp(argv[1], "companion") == 0) {
      if (argc < 3) {
        LOGI("Usage: zygiskd companion <fd>");

        return 1;
      }

      int fd = atoi(argv[2]);
      companion_entry(fd);

      return 0;
    }

    else if (strcmp(argv[1], "version") == 0) {
      LOGI("ReZygisk Daemon %s", ZKSU_VERSION);

      return 0;
    }

    else if (strcmp(argv[1], "root") == 0) {
      root_impls_setup();

      struct root_impl impl;
      get_impl(&impl);

      char impl_name[LONGEST_ROOT_IMPL_NAME];
      stringify_root_impl_name(impl, impl_name);

      LOGI("Root implementation: %s", impl_name);

      return 0;
    }

    else {
      LOGI("Usage: zygiskd [companion|version|root]");

      return 0;
    }
  }

  if (switch_mount_namespace(1) == false) {
    LOGE("Failed to switch mount namespace");

    return 1;
  }
  root_impls_setup();
  zygiskd_start(argv);

  return 0;
}
