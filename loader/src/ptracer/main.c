#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daemon.h"
#include "monitor.h"

int main(int argc, char **argv) {
  printf("The ReZygisk Tracer %s\n\n", ZKSU_VERSION);

  if (argc >= 2 && strcmp(argv[1], "monitor") == 0) {
    init_monitor();

    return 0;
  } else if (argc >= 3 && strcmp(argv[1], "trace") == 0) {
      bool is_tango = false;
      bool do_restart = false;

      for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--restart") == 0) do_restart = true;
        else if (strcmp(argv[i], "--tango") == 0) is_tango = true;
      }

      /* INFO: We need to be fast enough to not miss Tango's injection point,
                 so we just delay for Tango Zygote. */
      if (do_restart && !is_tango) rezygiskd_zygote_restart();

      long pid = strtol(argv[2], 0, 0);
      if (!trace_zygote(pid, is_tango)) {
        kill(pid, SIGKILL);

        return 1;
      }

      if (do_restart && is_tango) rezygiskd_zygote_restart();

      return 0;
  } else if (argc >= 2 && strcmp(argv[1], "ctl") == 0) {
    enum rezygiskd_command command;

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
    struct rezygisk_info info;
    rezygiskd_get_info(&info);

    printf("Daemon process PID: %d\n", info.pid);

    switch (info.root_impl) {
      case ROOT_IMPL_NONE: {
        printf("Root implementation: none\n");

        break;
      }
      case ROOT_IMPL_APATCH: {
        printf("Root implementation: APatch\n");

        break;
      }
      case ROOT_IMPL_KERNELSU: {
        printf("Root implementation: KernelSU\n");

        break;
      }
      case ROOT_IMPL_MAGISK: {
        printf("Root implementation: Magisk\n");

        break;
      }
    }

    if (info.modules.modules_count != 0) {
      printf("Modules: %zu\n", info.modules.modules_count);

      for (size_t i = 0; i < info.modules.modules_count; i++) {
        printf(" - %s\n", info.modules.modules[i]);
      }
    } else {
      printf("Modules: N/A\n");
    }

    free_rezygisk_info(&info);

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
