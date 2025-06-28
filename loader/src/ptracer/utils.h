#ifndef UTILS_H
#define UTILS_H

#include <sys/ptrace.h>

#include "daemon.h"

#ifdef __LP64__
  #define LOG_TAG "zygisk-ptrace64"
#else
  #define LOG_TAG "zygisk-ptrace32"
#endif

#include "logging.h"

struct map {
  uintptr_t start;
  uintptr_t end;
  uint8_t perms;
  bool is_private;
  uintptr_t offset;
  dev_t dev;
  ino_t inode;
  const char *path;
};

struct maps {
  struct map *maps;
  size_t size;
};

struct maps *parse_maps(const char *filename);

void free_maps(struct maps *maps);

#if defined(__x86_64__)
  #define REG_SP rsp
  #define REG_IP rip
  #define REG_RET rax
  #define REG_SYSNR orig_rax
#elif defined(__i386__)
  #define REG_SP esp
  #define REG_IP eip
  #define REG_RET eax
  #define REG_SYSNR orig_eax
#elif defined(__aarch64__)
  #define REG_SP sp
  #define REG_IP pc
  #define REG_RET regs[0]
  #define REG_SYSNR regs[8]
#elif defined(__arm__)
  #define REG_SP uregs[13]
  #define REG_IP uregs[15]
  #define REG_RET uregs[0]
  #define REG_SYSNR uregs[7]
  #define user_regs_struct user_regs
#endif

ssize_t write_proc(int pid, uintptr_t remote_addr, const void *buf, size_t len);

ssize_t read_proc(int pid, uintptr_t remote_addr, void *buf, size_t len);

bool get_regs(int pid, struct user_regs_struct *regs);

bool set_regs(int pid, struct user_regs_struct *regs);

void get_addr_mem_region(struct maps *map, uintptr_t addr, char *buf, size_t buf_size);

const char *position_after(const char *str, const char needle);

void *find_module_return_addr(struct maps *map, const char *suffix);

void *find_func_addr(struct maps *local_info, struct maps *remote_info, const char *module, const char *func);

void align_stack(struct user_regs_struct *regs, long preserve);

uintptr_t push_string(int pid, struct user_regs_struct *regs, const char *str);

uintptr_t remote_call(int pid, struct user_regs_struct *regs, uintptr_t func_addr, uintptr_t return_addr, long *args, size_t args_size);

int fork_dont_care();

void wait_for_trace(int pid, int* status, int flags);

void parse_status(int status, char *buf, size_t len);

#define WPTEVENT(x) (x >> 16)

#define CASE_CONST_RETURN(x) case x: return #x;

static inline const char *parse_ptrace_event(int status) {
  status = status >> 16;

  switch (status) {
    CASE_CONST_RETURN(PTRACE_EVENT_FORK)
    CASE_CONST_RETURN(PTRACE_EVENT_VFORK)
    CASE_CONST_RETURN(PTRACE_EVENT_CLONE)
    CASE_CONST_RETURN(PTRACE_EVENT_EXEC)
    CASE_CONST_RETURN(PTRACE_EVENT_VFORK_DONE)
    CASE_CONST_RETURN(PTRACE_EVENT_EXIT)
    CASE_CONST_RETURN(PTRACE_EVENT_SECCOMP)
    CASE_CONST_RETURN(PTRACE_EVENT_STOP)
    default:
      return "(no event)";
  }
}

static inline const char *sigabbrev_np(int sig) {
  if (sig > 0 && sig < NSIG) return sys_signame[sig];

  return "(unknown)";
}

int get_program(int pid, char *buf, size_t size);

/* INFO: pid = 0, fd != nullptr -> set to fd
         pid != 0, fd != nullptr -> set to pid ns, give orig ns in fd
*/
bool switch_mnt_ns(int pid, int *fd);

#endif /* UTILS_H */