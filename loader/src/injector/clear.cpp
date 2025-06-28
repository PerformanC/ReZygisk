#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <string>

#include "logging.h"
#include "zygisk.hpp"

static bool seccomp_filters_visible() {
    std::ifstream statusFile("/proc/self/status");
    if (!statusFile.is_open()) {
        return true;
    }

    std::string line;
    while (std::getline(statusFile, line)) {
        if (line.find("Seccomp_filters:") == 0) {
            return true;
        }
    }

    return false;
}

void send_seccomp_event() {
    if (seccomp_filters_visible()) {
        return;
    }

    __u32 args[4] = {0};

    int rnd_fd = open("/dev/urandom", O_RDONLY);
    if (rnd_fd == -1) {
        PLOGE("send_seccomp_event: open(/dev/urandom)");
        return;
    }

    if (read(rnd_fd, &args, sizeof(args)) != sizeof(args)) {
        PLOGE("send_seccomp_event: read(rnd_fd)");
        close(rnd_fd);
        return;
    }

    close(rnd_fd);

    args[0] |= 0x10000;

    struct sock_filter filter[] = {
            // Check syscall number
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 0, 9),

            // Load and check arg0 (lower 32 bits)
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[0], 0, 7),

            // Load and check arg1 (lower 32 bits)
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[1], 0, 5),

            // Load and check arg2 (lower 32 bits)
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[2], 0, 3),

            // Load and check arg3 (lower 32 bits)
            BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[3])),
            BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, args[3], 0, 1),

            // All match: return TRACE => will trigger PTRACE_EVENT_SECCOMP
            BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE),

            // Default: allow
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
