#include <stdio.h>

#include "monitor.h"
#include "utils.hpp"
#include "daemon.h"

int main(int argc, char **argv) {
  zygiskd::Init("/data/adb/rezygisk");

  printf("The ReZygisk Tracer %s\n\n", ZKSU_VERSION);

  if (argc >= 2 && strcmp(argv[1], "monitor") == 0) {
    init_monitor();

    printf("[ReZygisk]: Started monitoring...\n");

    return 0;
  } else if (argc >= 3 && strcmp(argv[1], "trace") == 0) {
      if (argc >= 4 && strcmp(argv[3], "--restart") == 0) zygiskd::ZygoteRestart();

      long pid = strtol(argv[2], 0, 0);
      if (!trace_zygote(pid)) {
        kill(pid, SIGKILL);

        return 1;
      }

      printf("[ReZygisk]: Tracing %ld...\n", pid);

      return 0;
  } else if (argc >= 2 && strcmp(argv[1], "ctl") == 0) {
    enum Command command;

    if (strcmp(argv[2], "start") == 0) command = START;
    else if (strcmp(argv[2], "stop") == 0) command = STOP;
    else if (strcmp(argv[2], "exit") == 0) command = EXIT;
    else {
      printf("[ReZygisk]: Usage: %s ctl <start|stop|exit>\n", argv[0]);

      return 1;
    }

    if (send_control_command(command) == -1) {
      printf("[ReZygisk]: Failed to send the command, is the daemon running?\n");

      return 1;
    }

    printf("[ReZygisk]: command sent\n");

    return 0;
  } else if (argc >= 2 && strcmp(argv[1], "version") == 0) {
    /* INFO: Noop*/

    return 0;
  } else if (argc >= 2 && strcmp(argv[1], "info") == 0) {
    struct zygote_info info;
    zygiskd::GetInfo(&info);

    printf("Daemon process PID: %d\n", info.pid);

    switch (info.root_impl) {
      case ZYGOTE_ROOT_IMPL_NONE: {
        printf("Root implementation: none\n");

        break;
      }
      case ZYGOTE_ROOT_IMPL_APATCH: {
        printf("Root implementation: APatch\n");
        
        break;
      }
      case ZYGOTE_ROOT_IMPL_KERNELSU: {
        printf("Root implementation: KernelSU\n");

        break;
      }
      case ZYGOTE_ROOT_IMPL_MAGISK: {
        printf("Root implementation: Magisk\n");

        break;
      }
    }

    printf("Is the daemon running: %s\n", info.running ? "yes" : "no");

    if (info.modules->modules_count != 0) {
      printf("Modules: %zu\n", info.modules->modules_count);

      for (size_t i = 0; i < info.modules->modules_count; i++) {
        printf(" - %s\n", info.modules->modules[i]);

        free(info.modules->modules[i]);
      }

      free(info.modules->modules);
    } else {
      printf("Modules: N/A\n");
    }

    return 0;
  } else {
    printf(
      "Available commands:\n"
      " - monitor\n"
      " - trace <pid> [--restart]\n"
      " - ctl <start|stop|exit>\n"
      " - version: Shows the version of ReZygisk.\n"
      " - info: Shows information about the created daemon/injection.\n"
      "\n"
      "<...>: Obligatory\n"
      "[...]: Optional\n");

    return 1;
  }
}
