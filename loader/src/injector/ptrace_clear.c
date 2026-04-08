#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <fcntl.h>
#include <sys/syscall.h>

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "logging.h"

#include "ptrace_clear.h"

static bool seccomp_filters_visible() {
  FILE *status_file = fopen("/proc/self/status", "r");
  if (!status_file) {
    PLOGE("open /proc/self/status");

    return true;
  }

  char line[256];
  while (fgets(line, sizeof(line), status_file)) {
    if (strncmp(line, "Seccomp_filters:", strlen("Seccomp_filters:")) != 0) continue;

    fclose(status_file);

    return true;
  }

  fclose(status_file);

  return false;
}

void perform_ptrace_message_clear() {
  /* INFO: Since kernel 5.10, Seccomp filters are visible, making hiding via seccomp event unusable */
  if (seccomp_filters_visible()) {
    LOGD("Seccomp filters are visible, skipping using hiding via seccomp event");

    return;
  }

  int rnd_fd = open("/dev/urandom", O_RDONLY);
  if (rnd_fd == -1) {
    PLOGE("open /dev/urandom");

    return;
  }

  uint32_t args[4] = { 0 };
  if (read(rnd_fd, &args, sizeof(args)) != sizeof(args)) {
    PLOGE("read /dev/urandom");

    close(rnd_fd);

    return;
  }

  close(rnd_fd);

  args[0] |= 0x10000;

  struct sock_filter filter[] = {
    /* INFO: Check syscall number */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 0, 9),

    /* INFO: Load and check arg0 (lower 32 bits) */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[0], 0, 7),

    /* INFO: Load and check arg1 (lower 32 bits) */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[1], 0, 5),

    /* INFO: Load and check arg2 (lower 32 bits) */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[2], 0, 3),

    /* INFO: Load and check arg3 (lower 32 bits) */
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[3])),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[3], 0, 1),

    /* INFO: All match: return TRACE => will trigger PTRACE_EVENT_SECCOMP */
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE),

    /* INFO: Default: allow */
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };

  struct sock_fprog prog = {
    .len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
    .filter = filter,
  };

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
    PLOGE("prctl(SECCOMP)");

    return;
  }

  /* INFO: This will trigger a ptrace event, syscall will not execute due to tracee_skip_syscall */
  syscall(__NR_exit_group, args[0], args[1], args[2], args[3]);
}
