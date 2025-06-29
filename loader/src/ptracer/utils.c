#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

#include <sys/sysmacros.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/auxv.h>
#include <sys/uio.h>
#include <signal.h>
#include <dlfcn.h>
#include <sched.h>
#include <fcntl.h>
#include <link.h>

#include <unistd.h>
#include <linux/limits.h>

#include "elf_util.h"

#include "utils.h"

bool switch_mnt_ns(int pid, int *fd) {
  int nsfd, old_nsfd = -1;

  char path[PATH_MAX];
  if (pid == 0) {
    if (fd != NULL) {
      nsfd = *fd;
      *fd = -1;
    } else return false;

    snprintf(path, sizeof(path), "/proc/self/fd/%d", nsfd);
  } else {
    if (fd != NULL) {
      old_nsfd = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
      if (old_nsfd == -1) {
        PLOGE("get old nsfd");

        return false;
      }

      *fd = old_nsfd;
    }

    snprintf(path, sizeof(path), "/proc/%d/ns/mnt", pid);

    nsfd = open(path, O_RDONLY | O_CLOEXEC);
    if (nsfd == -1) {
      PLOGE("open nsfd %s", path);

      close(old_nsfd);

      return false;
    }
  }

  if (setns(nsfd, CLONE_NEWNS) == -1) {
    PLOGE("set ns to %s", path);

    close(nsfd);
    close(old_nsfd);

    return false;
  }

  close(nsfd);

  return true;
}

struct maps *parse_maps(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    LOGE("Failed to open %s", filename);

    return NULL;
  }

  struct maps *maps = (struct maps *)malloc(sizeof(struct maps));
  if (!maps) {
    LOGE("Failed to allocate memory for maps");

    fclose(fp);

    return NULL;
  }

  /* INFO: To ensure in the realloc the libc will know it is meant
             to allocate, and not reallocate from a garbage address. */
  maps->maps = NULL;

  char line[4096 * 2];
  size_t i = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    /* INFO: Remove line ending at the end */
    line[strlen(line) - 1] = '\0';

    uintptr_t addr_start;
    uintptr_t addr_end;
    uintptr_t addr_offset;
    ino_t inode;
    unsigned int dev_major;
    unsigned int dev_minor;
    char permissions[5] = "";
    int path_offset;

    sscanf(line,
           "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %x:%x %lu %n%*s",
           &addr_start, &addr_end, permissions, &addr_offset, &dev_major, &dev_minor,
           &inode, &path_offset);

    while (isspace(line[path_offset])) {
      path_offset++;
    }

    maps->maps = (struct map *)realloc(maps->maps, (i + 1) * sizeof(struct map));
    if (!maps->maps) {
      LOGE("Failed to allocate memory for maps->maps");

      maps->size = i;

      fclose(fp);
      free_maps(maps);

      return NULL;
    }

    maps->maps[i].start = addr_start;
    maps->maps[i].end = addr_end;
    maps->maps[i].offset = addr_offset;

    maps->maps[i].perms = 0;
    if (permissions[0] == 'r') maps->maps[i].perms |= PROT_READ;
    if (permissions[1] == 'w') maps->maps[i].perms |= PROT_WRITE;
    if (permissions[2] == 'x') maps->maps[i].perms |= PROT_EXEC;

    maps->maps[i].is_private = permissions[3] == 'p';
    maps->maps[i].dev = makedev(dev_major, dev_minor);
    maps->maps[i].inode = inode;
    maps->maps[i].path = strdup(line + path_offset);
    if (!maps->maps[i].path) {
      LOGE("Failed to allocate memory for maps->maps[%zu].path", i);

      maps->size = i;

      fclose(fp);
      free_maps(maps);

      return NULL;
    }

    i++;
  }

  fclose(fp);

  maps->size = i;

  return maps;
}

void free_maps(struct maps *maps) {
  if (!maps) {
    return;
  }

  for (size_t i = 0; i < maps->size; i++) {
    free((void *)maps->maps[i].path);
  }

  free(maps->maps);
  free(maps);
}

ssize_t write_proc(int pid, uintptr_t remote_addr, const void *buf, size_t len) {
  LOGV("write to remote addr %" PRIxPTR " size %zu", remote_addr, len);

  struct iovec local = {
    .iov_base = (void *)buf,
    .iov_len = len
  };

  struct iovec remote = {
    .iov_base = (void *)remote_addr,
    .iov_len = len
  };

  ssize_t l = process_vm_writev(pid, &local, 1, &remote, 1, 0);
  if (l == -1) PLOGE("process_vm_writev");
  else if ((size_t)l != len) LOGW("not fully written: %zu, excepted %zu", l, len);

  return l;
}

ssize_t read_proc(int pid, uintptr_t remote_addr, void *buf, size_t len) {
  struct iovec local = {
    .iov_base = (void *)buf,
    .iov_len = len
  };

  struct iovec remote = {
    .iov_base = (void *)remote_addr,
    .iov_len = len
  };

  ssize_t l = process_vm_readv(pid, &local, 1, &remote, 1, 0);
  if (l == -1) PLOGE("process_vm_readv");
  else if ((size_t)l != len) LOGW("not fully read: %zu, excepted %zu", l, len);

  return l;
}

bool get_regs(int pid, struct user_regs_struct *regs) {
  #if defined(__x86_64__) || defined(__i386__)
    if (ptrace(PTRACE_GETREGS, pid, 0, regs) == -1) {
      PLOGE("getregs");

      return false;
    }
  #elif defined(__aarch64__) || defined(__arm__)
    struct iovec iov = {
      .iov_base = regs,
      .iov_len = sizeof(struct user_regs_struct),
    };

    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &iov) == -1) {
      PLOGE("getregs");

      return false;
    }
  #endif

  return true;
}

bool set_regs(int pid, struct user_regs_struct *regs) {
  #if defined(__x86_64__) || defined(__i386__)
    if (ptrace(PTRACE_SETREGS, pid, 0, regs) == -1) {
      PLOGE("setregs");

      return false;
    }
  #elif defined(__aarch64__) || defined(__arm__)
    struct iovec iov = {
      .iov_base = regs,
      .iov_len = sizeof(struct user_regs_struct),
    };

    if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &iov) == -1) {
      PLOGE("setregs");

      return false;
    }
  #endif

  return true;
}

void get_addr_mem_region(struct maps *info, uintptr_t addr, char *buf, size_t buf_size) {
  for (size_t i = 0; i < info->size; i++) {
    /* TODO: Early "leave" */
    if (info->maps[i].start <= addr && info->maps[i].end > addr) {
      snprintf(buf, buf_size, "%s %s%s%s",
               info->maps[i].path,
               info->maps[i].perms & PROT_READ ? "r" : "-",
               info->maps[i].perms & PROT_WRITE ? "w" : "-",
               info->maps[i].perms & PROT_EXEC ? "x" : "-");

      return;
    }
  }

  snprintf(buf, buf_size, "<unknown>");
}

/* INFO: strrchr but without modifying the string */
const char *position_after(const char *str, const char needle) {
  const char *positioned = str + strlen(str);

  int i = strlen(str);
  while (i != 0) {
    i--;
    if (str[i] == needle) {
      positioned = str + i + 1;

      break;
    }
  }

  return positioned;
}

void *find_module_return_addr(struct maps *map, const char *suffix) {
  for (size_t i = 0; i < map->size; i++) {
    /* TODO: Make it NULL in 1 length path */
    if (map->maps[i].path == NULL) continue;

    const char *file_name = position_after(map->maps[i].path, '/');
    if (!file_name) continue;

    if (strlen(file_name) < strlen(suffix) || (map->maps[i].perms & PROT_EXEC) != 0 || strncmp(file_name, suffix, strlen(suffix)) != 0) continue;

    return (void *)map->maps[i].start;
  }

  return NULL;
}

void *find_module_base(struct maps *map, const char *file) {
  for (size_t i = 0; i < map->size; i++) {
    if (map->maps[i].path == NULL) continue;

    const char *file_path = map->maps[i].path;

    if (strlen(file_path) != strlen(file) || map->maps[i].offset != 0 || strncmp(file_path, file, strlen(file)) != 0) continue;

    return (void *)map->maps[i].start;
  }

  return NULL;
}

void *find_func_addr(struct maps *local_info, struct maps *remote_info, const char *module, const char *func) {
  uint8_t *local_base = (uint8_t *)find_module_base(local_info, module);
  if (local_base == NULL) {
    LOGE("failed to find local base for module %s", module);

    return NULL;
  }

  uint8_t *remote_base = (uint8_t *)find_module_base(remote_info, module);
  if (remote_base == NULL) {
    LOGE("failed to find remote base for module %s", module);

    return NULL;
  }

  LOGD("found local base %p remote base %p", local_base, remote_base);

  ElfImg *mod = ElfImg_create(module, local_base);
  if (mod == NULL) {
    LOGE("failed to create elf img %s", module);

    return NULL;
  }

  uint8_t *sym = (uint8_t *)getSymbAddress(mod, func);
  if (sym == NULL) {
    LOGE("failed to find symbol %s in %s", func, module);

    ElfImg_destroy(mod);

    return NULL;
  }

  LOGD("found symbol %s in %s: %p", func, module, sym);

  uintptr_t addr = (uintptr_t)(sym - local_base) + (uintptr_t)remote_base;
  LOGD("addr %p", (void *)addr);

  ElfImg_destroy(mod);

  return (void *)addr;
}

void align_stack(struct user_regs_struct *regs, long preserve) {
  /* INFO: ~0xf is a negative value, and REG_SP is unsigned,
             so we must cast REG_SP to signed type before subtracting
             then cast back to unsigned type.
  */
  regs->REG_SP = (uintptr_t)((intptr_t)(regs->REG_SP - preserve) & ~0xf);
}

uintptr_t push_string(int pid, struct user_regs_struct *regs, const char *str) {
  size_t len = strlen(str) + 1;

  regs->REG_SP -= len;

  align_stack(regs, 0);

  uintptr_t addr = (uintptr_t)regs->REG_SP;
  if (!write_proc(pid, addr, str, len)) LOGE("failed to write string %s", str);

  LOGD("pushed string %" PRIxPTR, addr);

  return addr;
}

uintptr_t remote_call(int pid, struct user_regs_struct *regs, uintptr_t func_addr, uintptr_t return_addr, long *args, size_t args_size) {
  align_stack(regs, 0);

  LOGV("calling remote function %" PRIxPTR " args %zu", func_addr, args_size);

  for (size_t i = 0; i < args_size; i++) {
    LOGV("arg %p", (void *)args[i]);
  }

  #if defined(__x86_64__)
    if (args_size >= 1) regs->rdi = args[0];
    if (args_size >= 2) regs->rsi = args[1];
    if (args_size >= 3) regs->rdx = args[2];
    if (args_size >= 4) regs->rcx = args[3];
    if (args_size >= 5) regs->r8 = args[4];
    if (args_size >= 6) regs->r9 = args[5];
    if (args_size > 6) {
      long remain = (args_size - 6L) * sizeof(long);
      align_stack(regs, remain);

      if (!write_proc(pid, (uintptr_t) regs->REG_SP, args, remain)) LOGE("failed to push arguments");
    }

    regs->REG_SP -= sizeof(long);

    if (!write_proc(pid, (uintptr_t) regs->REG_SP, &return_addr, sizeof(return_addr))) LOGE("failed to write return addr");

    regs->REG_IP = func_addr;
  #elif defined(__i386__)
    if (args_size > 0) {
      long remain = (args_size) * sizeof(long);
      align_stack(regs, remain);

      if (!write_proc(pid, (uintptr_t) regs->REG_SP, args, remain)) LOGE("failed to push arguments");
    }

    regs->REG_SP -= sizeof(long);

    if (!write_proc(pid, (uintptr_t) regs->REG_SP, &return_addr, sizeof(return_addr))) LOGE("failed to write return addr");

    regs->REG_IP = func_addr;
  #elif defined(__aarch64__)
    for (size_t i = 0; i < args_size && i < 8; i++) {
      regs->regs[i] = args[i];
    }

    if (args_size > 8) {
      long remain = (args_size - 8) * sizeof(long);
      align_stack(regs, remain);

      write_proc(pid, (uintptr_t)regs->REG_SP, args, remain);
    }

    regs->regs[30] = return_addr;
    regs->REG_IP = func_addr;
  #elif defined(__arm__)
    for (size_t i = 0; i < args_size && i < 4; i++) {
      regs->uregs[i] = args[i];
    }

    if (args_size > 4) {
      long remain = (args_size - 4) * sizeof(long);
      align_stack(regs, remain);

      write_proc(pid, (uintptr_t)regs->REG_SP, args, remain);
    }

    regs->uregs[14] = return_addr;
    regs->REG_IP = func_addr;

    unsigned long CPSR_T_MASK = 1lu << 5;

    if ((regs->REG_IP & 1) != 0) {
      regs->REG_IP = regs->REG_IP & ~1;
      regs->uregs[16] = regs->uregs[16] | CPSR_T_MASK;
    } else {
      regs->uregs[16] = regs->uregs[16] & ~CPSR_T_MASK;
    }
  #endif

  if (!set_regs(pid, regs)) {
    LOGE("failed to set regs");

    return 0;
  }

  ptrace(PTRACE_CONT, pid, 0, 0);

  int status;
  wait_for_trace(pid, &status, __WALL);
  if (!get_regs(pid, regs)) {
    LOGE("failed to get regs after call");

    return 0;
  }

  if (WSTOPSIG(status) == SIGSEGV) {
    if ((uintptr_t)regs->REG_IP != return_addr) {
      LOGE("wrong return addr %p", (void *) regs->REG_IP);

      return 0;
    }

    return regs->REG_RET;
  } else {
    char status_str[64];
    parse_status(status, status_str, sizeof(status_str));

    LOGE("stopped by other reason %s at addr %p", status_str, (void *)regs->REG_IP);
  }

  return 0;
}

int fork_dont_care() {
  pid_t pid = fork();

  if (pid < 0) PLOGE("fork 1");
  else if (pid == 0) {
    pid = fork();
    if (pid < 0) PLOGE("fork 2");
    else if (pid > 0) exit(0);
  } else {
    int status;
    waitpid(pid, &status, __WALL);
  }

  return pid;
}

void tracee_skip_syscall(int pid) {
  struct user_regs_struct regs;
  if (!get_regs(pid, &regs)) {
    LOGE("failed to get seccomp regs");
    exit(1);
  }
  regs.REG_SYSNR = -1;
  if (!set_regs(pid, &regs)) {
    LOGE("failed to set seccomp regs");
    exit(1);
  }

  /* INFO: It might not work, don't check for error */
#if defined(__aarch64__)
  int sysnr = -1;
  struct iovec iov = {
    .iov_base = &sysnr,
    .iov_len = sizeof (int),
  };
  ptrace(PTRACE_SETREGSET, pid, NT_ARM_SYSTEM_CALL, &iov);
#elif defined(__arm__)
  ptrace(PTRACE_SET_SYSCALL, pid, 0, (void*) -1);
#endif

}

void wait_for_trace(int pid, int *status, int flags) {
  while (1) {
    pid_t result = waitpid(pid, status, flags);
    if (result == -1) {
      if (errno == EINTR) continue;

      PLOGE("wait %d failed", pid);
      exit(1);
    }

    if (*status >> 8 == (SIGTRAP | (PTRACE_EVENT_SECCOMP << 8))) {
      tracee_skip_syscall(pid);

      ptrace(PTRACE_CONT, pid, 0, 0);

      continue;
    } else if (!WIFSTOPPED(*status)) {
      char status_str[64];
      parse_status(*status, status_str, sizeof(status_str));

      LOGE("process %d not stopped for trace: %s, exit", pid, status_str);

      exit(1);
    }

    return;
  }
}

void parse_status(int status, char *buf, size_t len) {
  snprintf(buf, len, "0x%x ", status);

  if (WIFEXITED(status)) {
    snprintf(buf + strlen(buf), len - strlen(buf), "exited with %d", WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    snprintf(buf + strlen(buf), len - strlen(buf), "signaled with %s(%d)", sigabbrev_np(WTERMSIG(status)), WTERMSIG(status));
  } else if (WIFSTOPPED(status)) {
    snprintf(buf + strlen(buf), len - strlen(buf), "stopped by ");

    int stop_sig = WSTOPSIG(status);
    snprintf(buf + strlen(buf), len - strlen(buf), "signal=%s(%d),", sigabbrev_np(stop_sig), stop_sig);
    snprintf(buf + strlen(buf), len - strlen(buf), "event=%s", parse_ptrace_event(status));
  } else {
    snprintf(buf + strlen(buf), len - strlen(buf), "unknown");
  }
}

int get_program(int pid, char *buf, size_t size) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/%d/exe", pid);

  ssize_t sz = readlink(path, buf, size);

  if (sz == -1) {
    PLOGE("readlink /proc/%d/exe", pid);

    return -1;
  }

  buf[sz] = '\0';

  return 0;
}
