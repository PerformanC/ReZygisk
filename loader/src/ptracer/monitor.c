#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include "daemon.h"
#include "socket_utils.h"
#include "utils.h"

#include "monitor.h"

#define SOCKET_NAME "init_monitor"

#define STOPPED_WITH(sig, event) (WIFSTOPPED(sigchld_status) && (sigchld_status >> 8 == ((sig) | ((event) << 8))))

static bool update_status(const char *message);

const char *monitor_stop_reason = NULL;

struct environment_information {
  char *root_impl;
  char **modules;
  uint32_t modules_len;
};

static struct environment_information environment_information64;
static struct environment_information environment_information32;

enum ptracer_tracing_state {
  TRACING,
  STOPPING,
  STOPPED,
  EXITING
};

enum ptracer_tracing_state tracing_state = TRACING;

struct rezygiskd_status {
  bool supported;
  bool zygote_injected;
  bool daemon_running;
  pid_t daemon_pid;
  char *daemon_info;
  char *daemon_error_info;
};

struct rezygiskd_status status64 = {
  .supported = false,
  .zygote_injected = false,
  .daemon_running = false,
  .daemon_pid = -1,
  .daemon_info = NULL,
  .daemon_error_info = NULL
};
struct rezygiskd_status status32 = {
  .supported = false,
  .zygote_injected = false,
  .daemon_running = false,
  .daemon_pid = -1,
  .daemon_info = NULL,
  .daemon_error_info = NULL
};

int monitor_epoll_fd;
bool monitor_events_running = true;
typedef void (*monitor_event_callback_t)();

bool monitor_events_init() {
  monitor_epoll_fd = epoll_create(1);
  if (monitor_epoll_fd == -1) {
    PLOGE("epoll_create");

    return false;
  }

  return true;
}

bool monitor_events_register_event(monitor_event_callback_t event_cb, int fd, uint32_t events) {
  struct epoll_event ev = {
    .data.ptr = (void *)event_cb,
    .events = events
  };

  if (epoll_ctl(monitor_epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    PLOGE("epoll_ctl");

    return false;
  }

  return true;
}

void monitor_events_stop() {
  monitor_events_running = false;
}

void monitor_events_loop() {
  struct epoll_event events[2];
  while (monitor_events_running) {
    int nfds = epoll_wait(monitor_epoll_fd, events, 2, -1);
    if (nfds == -1 && errno != EINTR) {
      PLOGE("epoll_wait");

      monitor_events_running = false;

      break;
    }

    for (int i = 0; i < nfds; i++) {
      if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        LOGE("Failed event on fd %d: %s", ((struct epoll_event *)&events[i])->data.fd, strerror(errno));

        monitor_events_running = false;

        break;
      }

      ((monitor_event_callback_t)events[i].data.ptr)();

      if (!monitor_events_running) break;
    }
  }

  if (monitor_epoll_fd >= 0) close(monitor_epoll_fd);
  monitor_epoll_fd = -1;
}

int monitor_sock_fd;

bool rezygiskd_listener_init() {
  monitor_sock_fd = socket(PF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (monitor_sock_fd == -1) {
    PLOGE("socket create");

    return false;
  }

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX,
    .sun_path = { 0 }
  };

  size_t sun_path_len = sprintf(addr.sun_path, "%s/%s", rezygiskd_get_path(), SOCKET_NAME);

  socklen_t socklen = sizeof(sa_family_t) + sun_path_len;
  if (bind(monitor_sock_fd, (struct sockaddr *)&addr, socklen) == -1) {
    PLOGE("bind socket");

    return false;
  }

  return true;
}

struct rzd_msg_header {
  uint8_t cmd;
  uint32_t len;
  char data[0];
} __attribute__((packed));

#define LONGEST_ROOT_IMPL_NAME sizeof("KernelSU Next")

struct rzd_info_payload {
  char impl[LONGEST_ROOT_IMPL_NAME];
  uint32_t modules_count;
  char data[0];
} __attribute__((packed));

void rezygiskd_listener_callback() {
  while (1) {
    ssize_t dlen = recv(monitor_sock_fd, NULL, 0, MSG_PEEK | MSG_TRUNC);
    if (dlen == -1) {
      if (errno == EINTR || errno == EWOULDBLOCK) break;

      PLOGE("recv socket size");

      continue;
    }

    if ((size_t)dlen < sizeof(struct rzd_msg_header)) {
      LOGE("Failed to receive header of message, datagram size: %zd", dlen);

      /* INFO: Consume the rest of the datagram to not loop */
      char tmp;
      recv(monitor_sock_fd, &tmp, sizeof(tmp), 0);

      continue;
    }

    struct rzd_msg_header *msg = malloc((size_t)dlen);
    if (msg == NULL) {
      PLOGE("malloc datagram");

      /* INFO: Consume the rest of the datagram to not loop */
      char tmp;
      recv(monitor_sock_fd, &tmp, sizeof(tmp), 0);

      continue;
    }

    ssize_t nread = recv(monitor_sock_fd, msg, (size_t)dlen, 0);
    if (nread == -1) {
      if (errno == EINTR || errno == EWOULDBLOCK) {
        free(msg);

        break;
      }

      PLOGE("recv socket");
      free(msg);

      continue;
    }

    if (nread != dlen) {
      LOGE("Failed to receive full datagram, expected %zd, got %zd", dlen, nread);

      free(msg);

      continue;
    }

    switch (msg->cmd) {
      case START: {
        if (tracing_state == STOPPING) {
          LOGI("Continue tracing init");

          tracing_state = TRACING;
        } else if (tracing_state == STOPPED) {
          LOGI("Start tracing init");

          ptrace(PTRACE_SEIZE, 1, 0, PTRACE_O_TRACEFORK);

          tracing_state = TRACING;
        }

        update_status(NULL);

        break;
      }
      case STOP: {
        if (tracing_state == TRACING) {
          LOGI("Stop tracing requested");

          tracing_state = STOPPING;
          monitor_stop_reason = "user requested";

          ptrace(PTRACE_INTERRUPT, 1, 0, 0);
          update_status(NULL);
        }

        break;
      }
      case EXIT: {
        LOGI("Prepare for exit ...");

        tracing_state = EXITING;
        monitor_stop_reason = "user requested";

        update_status(NULL);
        monitor_events_stop();

        break;
      }
      case ZYGOTE64_INJECTED:
      case ZYGOTE32_INJECTED: {
        LOGI("Received Zygote%s injected command", msg->cmd == ZYGOTE64_INJECTED ? "64" : "32");

        struct rezygiskd_status *status = msg->cmd == ZYGOTE64_INJECTED ? &status64 : &status32;
        status->zygote_injected = true;

        update_status(NULL);

        break;
      }
      case DAEMON64_SET_INFO:
      case DAEMON32_SET_INFO: {
        const char *daemon_name = (msg->cmd == DAEMON64_SET_INFO ? "ReZygiskd64" : "ReZygiskd32");

        LOGD("Received %s info", daemon_name);

        /* INFO: Don't try to parse the data if too small */
        if (msg->len != (size_t)nread - sizeof(struct rzd_msg_header) || msg->len < sizeof(struct rzd_info_payload)) {
          LOGE("truncated %s info", daemon_name);

          break;
        }

        struct environment_information *environment_information = msg->cmd == DAEMON64_SET_INFO ? &environment_information64 : &environment_information32;
        if (environment_information->root_impl) {
          LOGD("freeing old %s root impl", daemon_name);

          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;
        }

        struct rzd_info_payload *info = (void *)msg->data;
        environment_information->root_impl = strndup(info->impl, sizeof(info->impl));
        if (environment_information->root_impl == NULL) {
          PLOGE("malloc %s root impl", daemon_name);

          break;
        }

        LOGD("%s root impl: %s", daemon_name, environment_information->root_impl);

        if (environment_information->modules) {
          LOGD("freeing old %s modules", daemon_name);

          for (size_t i = 0; i < environment_information->modules_len; i++) {
            free((void *)environment_information->modules[i]);
          }

          free((void *)environment_information->modules);
          environment_information->modules = NULL;
        }

        environment_information->modules_len = info->modules_count;
        environment_information->modules = info->modules_count > 0 ? malloc(info->modules_count * sizeof(char *)) : NULL;
        if (info->modules_count > 0 && environment_information->modules == NULL) {
          PLOGE("malloc %s modules", daemon_name);

          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;

          break;
        }

        char *raw_modules = info->data;
        char *end = (char *)msg + nread;

        /* INFO: Retrieve the list of n NULL-terminated strings, one after the other */
        size_t i;
        for (i = 0; i < environment_information->modules_len; i++) {
          if (raw_modules >= end) {
            LOGE("Found truncated %s modules, expected %u, got %zu", daemon_name, environment_information->modules_len, i);

            goto set_info_modules_cleanup;
          }

          environment_information->modules[i] = strndup(raw_modules, (size_t)(end - raw_modules));
          if (environment_information->modules[i] == NULL) {
            PLOGE("malloc %s module name", daemon_name);

            goto set_info_modules_cleanup;
          }

          LOGD("%s module %zu: %s", daemon_name, i, environment_information->modules[i]);

          raw_modules += strlen(environment_information->modules[i]) + 1;
        }

        update_status(NULL);

        break;

        set_info_modules_cleanup:
          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;

          for (size_t j = 0; j < i; j++) {
            free((void *)environment_information->modules[j]);
          }

          free((void *)environment_information->modules);
          environment_information->modules = NULL;

          break;
      }
      case DAEMON64_SET_ERROR_INFO:
      case DAEMON32_SET_ERROR_INFO: {
        const char *daemon_name = (msg->cmd == DAEMON64_SET_ERROR_INFO ? "ReZygiskd64" : "ReZygiskd32");

        LOGD("Received %s error info", daemon_name);

        if (msg->len != (size_t)nread - sizeof(struct rzd_msg_header) || msg->len == 0) {
          LOGE("truncated %s error info", daemon_name);

          break;
        }

        struct rezygiskd_status *status = msg->cmd == DAEMON64_SET_ERROR_INFO ? &status64 : &status32;
        if (status->daemon_error_info) {
          LOGD("freeing old %s error info", daemon_name);

          free(status->daemon_error_info);
          status->daemon_error_info = NULL;
        }

        status->daemon_error_info = strndup(msg->data, msg->len);
        if (status->daemon_error_info == NULL) {
          PLOGE("malloc %s error info", daemon_name);

          break;
        }
        LOGD("%s error info: %s", daemon_name, status->daemon_error_info);

        update_status(NULL);

        break;
      }
    }

    free(msg);
  }
}

void rezygiskd_listener_stop() {
  if (monitor_sock_fd >= 0) close(monitor_sock_fd);
  monitor_sock_fd = -1;
}

#define MAX_RETRY_COUNT 5

#define CREATE_ZYGOTE_START_COUNTER(abi)             \
  struct timespec last_zygote##abi = {               \
    .tv_sec = 0,                                     \
    .tv_nsec = 0                                     \
  };                                                 \
                                                     \
  int count_zygote ## abi = 0;                       \
  bool should_stop_inject ## abi() {                 \
    struct timespec now = {};                        \
    clock_gettime(CLOCK_MONOTONIC, &now);            \
    if (now.tv_sec - last_zygote ## abi.tv_sec < 30) \
      count_zygote ## abi++;                         \
    else                                             \
      count_zygote ## abi = 0;                       \
                                                     \
    last_zygote##abi = now;                          \
                                                     \
    return count_zygote##abi >= MAX_RETRY_COUNT;     \
  }

CREATE_ZYGOTE_START_COUNTER(64)
CREATE_ZYGOTE_START_COUNTER(32)

static bool ensure_daemon_created(bool is_64bit) {
  struct rezygiskd_status *status = is_64bit ? &status64 : &status32;
  if (status->daemon_pid != -1) {
    LOGI("ReZygiskd%s already running", is_64bit ? "64" : "32");

    return status->daemon_running;
  }

  pid_t pid = fork();
  if (pid < 0) {
    PLOGE("create ReZygiskd%s", is_64bit ? "64" : "32");

    return false;
  }

  if (pid == 0) {
    char daemon_name[PATH_MAX] = "./bin/zygiskd";
    strcat(daemon_name, is_64bit ? "64" : "32");

    execl(daemon_name, daemon_name, NULL);

    PLOGE("exec ReZygiskd%s failed", is_64bit ? "64" : "32");

    exit(1);
  }

  status->supported = true;
  status->daemon_pid = pid;
  status->daemon_running = true;

  return true;
}

#define CHECK_DAEMON_EXIT(abi)                                    \
  if (status##abi.supported && pid == status##abi.daemon_pid) {   \
    char status_str[64];                                          \
    parse_status(sigchld_status, status_str, sizeof(status_str)); \
                                                                  \
    LOGW("daemon" #abi " pid %d exited: %s", pid, status_str);    \
    status##abi.daemon_running = false;                           \
                                                                  \
    if (!status##abi.daemon_error_info) {                         \
      status##abi.daemon_error_info = strdup(status_str);         \
      if (!status##abi.daemon_error_info) {                       \
        LOGE("malloc daemon" #abi " error info failed");          \
                                                                  \
        return;                                                   \
      }                                                           \
    }                                                             \
                                                                  \
    update_status(NULL);                                          \
                                                                  \
    continue;                                                     \
  }

#define APP_PROCESS "/system/bin/app_process"
#define APP_PROCESS_64 APP_PROCESS "64"
#define APP_PROCESS_32 APP_PROCESS "32"

#define PRE_INJECT(abi, is_64)                                        \
  if (strcmp(program, APP_PROCESS_ ## abi) == 0) {                    \
    tracer = "./bin/zygisk-ptrace" # abi;                             \
    is_tango = false;                                                 \
                                                                      \
    if (should_stop_inject ## abi()) {                                \
      LOGW("Zygote" # abi " restart too much times, stop injecting"); \
                                                                      \
      tracing_state = STOPPING;                                       \
      monitor_stop_reason = "Zygote crashed";                         \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                              \
                                                                      \
      break;                                                          \
    }                                                                 \
                                                                      \
    if (!ensure_daemon_created(is_64)) {                              \
      LOGW("ReZygiskd " #abi "-bit not running, stop injecting");     \
                                                                      \
      tracing_state = STOPPING;                                       \
      monitor_stop_reason = "ReZygiskd not running";                  \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                              \
                                                                      \
      break;                                                          \
    }                                                                 \
  }

#define PRE_INJECT_TANGO                                           \
  if (strcmp(program, "/system_ext/bin/tango_translator") == 0) {  \
    tracer = "./bin/zygisk-ptrace32";                              \
    is_tango = true;                                               \
                                                                   \
    if (should_stop_inject32()) {                                  \
      LOGW("Tango restart too many times, stop injecting");        \
                                                                   \
      tracing_state = STOPPING;                                    \
      monitor_stop_reason = "Zygote crashed";                      \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                           \
                                                                   \
      break;                                                       \
    }                                                              \
                                                                   \
    if (!ensure_daemon_created(false)) {                           \
      LOGW("ReZygiskd 32-bit not running, stop injecting");        \
                                                                   \
      tracing_state = STOPPING;                                    \
      monitor_stop_reason = "ReZygiskd not running";               \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                           \
                                                                   \
      break;                                                       \
    }                                                              \
  }

int sigchld_signal_fd;
struct signalfd_siginfo sigchld_fdsi;
int sigchld_status;

pid_t *sigchld_process;
size_t sigchld_process_count = 0;

static bool claim_init_tracer() {
  if (ptrace(PTRACE_SEIZE, 1, 0, PTRACE_O_TRACEFORK) == -1) {
    /* INFO: In cases where, for example, 2 ReZygisks were executed, the second
               process won't be able to seize init due to limitations of ptrace.
               In this case, we should just exit the second process to avoid
               conflicts. */
    if (errno == EPERM) {
      LOGW("Another process is already tracing init");

      update_status("❌ Multiple Zygisks functioning");
    } else {
      PLOGE("failed to seize init");
    }

    return false;
  }

  return true;
}

bool sigchld_listener_init() {
  sigchld_process = NULL;

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);

  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
    PLOGE("set sigprocmask");

    return false;
  }

  sigchld_signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sigchld_signal_fd == -1) {
    PLOGE("create signalfd");

    return false;
  }

  return true;
}

void sigchld_listener_callback() {
  while (1) {
    ssize_t s = read(sigchld_signal_fd, &sigchld_fdsi, sizeof(sigchld_fdsi));
    if (s == -1) {
      if (errno == EAGAIN) break;

      PLOGE("read signalfd");

      continue;
    }

    if (s != sizeof(sigchld_fdsi)) {
      LOGE("read %zu != %zu", s, sizeof(sigchld_fdsi));

      continue;
    }

    if (sigchld_fdsi.ssi_signo != SIGCHLD) {
      LOGE("No sigchld received");

      continue;
    }

    int pid;
    while ((pid = waitpid(-1, &sigchld_status, __WALL | WNOHANG)) != 0) {
      if (pid == -1) {
        /* INFO: Never treat -1 as a process: with no children left (ECHILD) this
                   would spin forever. The next SIGCHLD brings us back here. */
        if (errno != ECHILD) PLOGE("waitpid");

        break;
      }

      if (pid == 1) {
        if (STOPPED_WITH(SIGTRAP, PTRACE_EVENT_FORK)) {
          long child_pid;

          ptrace(PTRACE_GETEVENTMSG, pid, 0, &child_pid);

          LOGV("Forked %ld", child_pid);
        } else if (STOPPED_WITH(SIGTRAP, PTRACE_EVENT_STOP) && tracing_state == STOPPING) {
          if (ptrace(PTRACE_DETACH, 1, 0, 0) == -1) PLOGE("failed to detach init");

          tracing_state = STOPPED;

          LOGI("Stopped tracing init");

          continue;
        }

        if (WIFSTOPPED(sigchld_status)) {
          if (WPTEVENT(sigchld_status) == 0) {
            if (WSTOPSIG(sigchld_status) != SIGSTOP && WSTOPSIG(sigchld_status) != SIGTSTP && WSTOPSIG(sigchld_status) != SIGTTIN && WSTOPSIG(sigchld_status) != SIGTTOU) {
              LOGW("Injecting signal sent to init: %s %d", sigabbrev_np(WSTOPSIG(sigchld_status)), WSTOPSIG(sigchld_status));

              ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(sigchld_status));

              continue;
            } else {
              LOGW("Suppressing stop signal sent to init: %s %d", sigabbrev_np(WSTOPSIG(sigchld_status)), WSTOPSIG(sigchld_status));
            }
          }

          ptrace(PTRACE_CONT, pid, 0, 0);
        }

        continue;
      }

      CHECK_DAEMON_EXIT(64)
      CHECK_DAEMON_EXIT(32)

      pid_t state = 0;
      for (size_t i = 0; i < sigchld_process_count; i++) {
        if (sigchld_process[i] != pid) continue;

        state = sigchld_process[i];

        break;
      }

      if (state == 0) {
        LOGV("New process %d attached", pid);

        for (size_t i = 0; i < sigchld_process_count; i++) {
          if (sigchld_process[i] != 0) continue;

          sigchld_process[i] = pid;

          goto ptrace_process;
        }

        pid_t *new_sigchld_process = (pid_t *)realloc(sigchld_process, sizeof(pid_t) * (sigchld_process_count + 1));
        if (new_sigchld_process == NULL) {
          PLOGE("realloc sigchld_process");

          continue;
        }
        sigchld_process = new_sigchld_process;

        sigchld_process[sigchld_process_count] = pid;
        sigchld_process_count++;

        ptrace_process:

        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACEEXEC);
        ptrace(PTRACE_CONT, pid, 0, 0);

        continue;
      } else {
        if (STOPPED_WITH(SIGTRAP, PTRACE_EVENT_EXEC)) {
          char program[PATH_MAX];
          if (get_program(pid, program, sizeof(program)) == -1) {
            LOGW("failed to get program %d", pid);

            continue;
          }

          LOGV("%d program %s", pid, program);

          const char *tracer = NULL;
          bool is_tango = false;

          do {
            if (tracing_state != TRACING) {
              LOGD("Stopped injecting %d because status is not set to tracing", pid);

              break;
            }

            PRE_INJECT(64, true)
            PRE_INJECT(32, false)
            PRE_INJECT_TANGO

            if (tracer != NULL) {
              LOGD("Stopping %d (program: %s, tracer: %s, tango: %s)", pid, program, tracer, is_tango ? "yes" : "no");

              kill(pid, SIGSTOP);
              ptrace(PTRACE_CONT, pid, 0, 0);
              waitpid(pid, &sigchld_status, __WALL);

              if (!STOPPED_WITH(SIGSTOP, 0)) {
                LOGE("Failed to stop process %d", pid);

                break;
              }

              LOGD("Detaching %d", pid);
              ptrace(PTRACE_DETACH, pid, 0, SIGSTOP);

              {
                sigchld_status = 0;
                int p = fork_dont_care();

                if (p == 0) {
                  char pid_str[32];
                  sprintf(pid_str, "%d", pid);

                  LOGI("exec tracer command: %s trace %s --restart%s", tracer, pid_str, is_tango ? " --tango" : "");

                  /* INFO: Only restart companions if it's not the first time */
                  if ((strcmp(program, APP_PROCESS_64) == 0 && count_zygote64 > 1) || ((strcmp(program, APP_PROCESS_32) == 0 || is_tango) && count_zygote32 > 1)) {
                    if (is_tango) {
                      execl(tracer, basename(tracer), "trace", pid_str, "--restart", "--tango", NULL);
                    } else {
                      execl(tracer, basename(tracer), "trace", pid_str, "--restart", NULL);
                    }
                  } else {
                    if (is_tango) {
                      execl(tracer, basename(tracer), "trace", pid_str, "--tango", NULL);
                    } else {
                      execl(tracer, basename(tracer), "trace", pid_str, NULL);
                    }
                  }

                  PLOGE("exec");

                  kill(pid, SIGKILL);
                  exit(1);
                } else if (p == -1) {
                  PLOGE("fork");

                  kill(pid, SIGKILL);
                }
              }
            }
          } while (false);
        } else {
          char status_str[64];
          parse_status(sigchld_status, status_str, sizeof(status_str));

          LOGW("process %d received unknown sigchld_status %s", pid, status_str);
        }

        for (size_t i = 0; i < sigchld_process_count; i++) {
          if (sigchld_process[i] != pid) continue;

          sigchld_process[i] = 0;

          break;
        }

        if (WIFSTOPPED(sigchld_status)) {
          LOGV("detach process %d", pid);

          ptrace(PTRACE_DETACH, pid, 0, 0);
        }
      }
    }
  }
}

void sigchld_listener_stop() {
  if (sigchld_signal_fd >= 0) close(sigchld_signal_fd);
  sigchld_signal_fd = -1;

  if (sigchld_process != NULL) free(sigchld_process);
  sigchld_process = NULL;
  sigchld_process_count = 0;
}

static char pre_section[1024];
static char post_section[1024];

#define WRITE_STATUS_ABI(suffix)                                                             \
  if (status ## suffix.supported) {                                                          \
    strlcat(status_text, ", ReZygisk " # suffix "-bit: ", sizeof(status_text));              \
                                                                                             \
    if (tracing_state != TRACING) strlcat(status_text, "❌", sizeof(status_text));           \
    else if (status ## suffix.zygote_injected && status ## suffix.daemon_running)            \
      strlcat(status_text, "✅", sizeof(status_text));                                       \
    else strlcat(status_text, "⚠️", sizeof(status_text));                                    \
                                                                                             \
    if (!status ## suffix.daemon_running) {                                                  \
      if (status ## suffix.daemon_error_info) {                                              \
        strlcat(status_text, "(ReZygiskd: ", sizeof(status_text));                           \
        strlcat(status_text, status ## suffix.daemon_error_info, sizeof(status_text));       \
        strlcat(status_text, ")", sizeof(status_text));                                      \
      } else {                                                                               \
        strlcat(status_text, "(ReZygiskd: not running)", sizeof(status_text));               \
      }                                                                                      \
    }                                                                                        \
  }

/* INFO: Writes a JSON string literal, escaping what could break the document. */
static void write_json_string(FILE *json, const char *str) {
  fputc('"', json);

  for (const unsigned char *c = (const unsigned char *)str; *c; c++) {
    switch (*c) {
      case '"': fputs("\\\"", json); break;
      case '\\': fputs("\\\\", json); break;
      case '\n': fputs("\\n", json); break;
      case '\r': fputs("\\r", json); break;
      case '\t': fputs("\\t", json); break;
      default: {
        if (*c < 0x20) fprintf(json, "\\u%04x", *c);
        else fputc(*c, json);
      }
    }
  }

  fputc('"', json);
}

static void write_json_daemon(FILE *json, const char *abi, struct rezygiskd_status *status, char **modules, uint32_t modules_len) {
  fprintf(json, "    \"%s\": {\n", abi);
  fprintf(json, "      \"state\": %d,\n", status->daemon_running);
  if (status->daemon_error_info) {
    fprintf(json, "      \"reason\": ");
    write_json_string(json, status->daemon_error_info);
    fprintf(json, ",\n");
  }
  fprintf(json, "      \"modules\": [");

  if (modules) for (uint32_t i = 0; i < modules_len; i++) {
    if (i > 0) fprintf(json, ", ");
    write_json_string(json, modules[i]);
  }

  fprintf(json, "]\n");
  fprintf(json, "    }");
}

#define STATE_JSON_PATH "/data/adb/rezygisk/state.json"
#define STATE_JSON_TMP_PATH STATE_JSON_PATH ".tmp"

static bool update_status(const char *message) {
  FILE *prop = fopen("/data/adb/modules/rezygisk/module.prop", "w");
  if (prop == NULL) {
    PLOGE("failed to open prop");

    return false;
  }

  if (message) {
    fprintf(prop, "%s[%s] %s", pre_section, message, post_section);
    fclose(prop);

    return true;
  }

  char status_text[512] = "Monitor: ";
  switch (tracing_state) {
    case TRACING: {
      strlcat(status_text, "✅", sizeof(status_text));

      break;
    }
    case STOPPING: [[fallthrough]];
    case STOPPED: {
      strlcat(status_text, "⛔", sizeof(status_text));

      break;
    }
    case EXITING: {
      strlcat(status_text, "❌", sizeof(status_text));

      break;
    }
  }

  WRITE_STATUS_ABI(64)
  WRITE_STATUS_ABI(32)

  fprintf(prop, "%s[%s] %s", pre_section, status_text, post_section);
  fclose(prop);

  if (environment_information64.root_impl || environment_information32.root_impl) {
    /* INFO: Write to a temporary file and rename it, so readers (WebUI) never
               see a truncated or half-written document. */
    FILE *json = fopen(STATE_JSON_TMP_PATH, "w");
    if (json == NULL) {
      PLOGE("failed to open state.json");

      return false;
    }

    fprintf(json, "{\n");
    fprintf(json, "  \"root\": ");
    write_json_string(json, environment_information64.root_impl ? environment_information64.root_impl : environment_information32.root_impl);
    fprintf(json, ",\n");

    fprintf(json, "  \"monitor\": {\n");
    fprintf(json, "    \"state\": \"%d\"", tracing_state);
    if (monitor_stop_reason) {
      fprintf(json, ",\n    \"reason\": ");
      write_json_string(json, monitor_stop_reason);
    }
    fprintf(json, "\n");

    if (status64.supported || status32.supported)
      fprintf(json, "  },\n");
    else
      fprintf(json, "  }\n");

    if (status64.supported || status32.supported) {
      fprintf(json, "  \"rezygiskd\": {\n");
      if (status64.supported) {
        write_json_daemon(json, "64", &status64, environment_information64.modules, environment_information64.modules_len);
        fprintf(json, status32.supported ? ",\n" : "\n");
      }

      if (status32.supported) {
        write_json_daemon(json, "32", &status32, environment_information32.modules, environment_information32.modules_len);
        fprintf(json, "\n");
      }

      fprintf(json, "  },\n");

      /* INFO: Always report every supported ABI, so a non-injected 32-bit
                 Zygote is shown as such instead of silently missing. */
      fprintf(json, "  \"zygote\": {\n");
      if (status64.supported) {
        fprintf(json, "    \"64\": %d", status64.zygote_injected);
        fprintf(json, status32.supported ? ",\n" : "\n");
      }
      if (status32.supported) {
        fprintf(json, "    \"32\": %d\n", status32.zygote_injected);
      }
      fprintf(json, "  }\n");
    }

    fprintf(json, "}\n");

    if (fclose(json) == EOF) {
      PLOGE("failed to write state.json");
      unlink(STATE_JSON_TMP_PATH);

      return false;
    }

    if (rename(STATE_JSON_TMP_PATH, STATE_JSON_PATH) == -1) {
      PLOGE("failed to rename state.json");
      unlink(STATE_JSON_TMP_PATH);

      return false;
    }
  } else {
    if (remove(STATE_JSON_PATH) == -1 && errno != ENOENT) {
      PLOGE("failed to remove state.json");
    }
  }

  LOGI("status updated: %s", status_text);

  return true;
}

static bool prepare_environment() {
  FILE *orig_prop = fopen("/data/adb/modules/rezygisk/module.prop", "r");
  if (orig_prop == NULL) {
    PLOGE("failed to open orig prop");

    return false;
  }

  bool after_description = false;

  char line[1024];
  while (fgets(line, sizeof(line), orig_prop) != NULL) {
    if (strncmp(line, "description=", strlen("description=")) == 0) {
      strlcat(pre_section, "description=", sizeof(pre_section));
      strlcat(post_section, line + strlen("description="), sizeof(post_section));
      after_description = true;

      continue;
    }

    if (after_description) strlcat(post_section, line, sizeof(post_section));
    else strlcat(pre_section, line, sizeof(pre_section));
  }

  fclose(orig_prop);

  return true;
}

void init_monitor() {
  LOGI("ReZygisk %s", ZKSU_VERSION);

  if (!prepare_environment()) exit(1);

  if (!claim_init_tracer()) exit(1);

  monitor_events_init();

  if (!rezygiskd_listener_init()) {
    LOGE("failed to create socket");

    close(monitor_epoll_fd);

    exit(1);
  }

  monitor_events_register_event(rezygiskd_listener_callback, monitor_sock_fd, EPOLLIN | EPOLLET);

  if (sigchld_listener_init() == false) {
    LOGE("failed to create signalfd");

    rezygiskd_listener_stop();
    close(monitor_epoll_fd);

    exit(1);
  }

  monitor_events_register_event(sigchld_listener_callback, sigchld_signal_fd, EPOLLIN | EPOLLET);

  monitor_events_loop();

  /* INFO: Once it stops the loop, we cannot access the epool data, so we
             either manually call the stops or save to a structure. */
  rezygiskd_listener_stop();
  sigchld_listener_stop();

  if (status64.daemon_info) free(status64.daemon_info);
  if (status64.daemon_error_info) free(status64.daemon_error_info);
  if (status32.daemon_info) free(status32.daemon_info);
  if (status32.daemon_error_info) free(status32.daemon_error_info);

  if (environment_information64.root_impl) free((void *)environment_information64.root_impl);
  if (environment_information64.modules) {
    for (uint32_t i = 0; i < environment_information64.modules_len; i++) {
      free((void *)environment_information64.modules[i]);
    }
    free((void *)environment_information64.modules);
  }

  if (environment_information32.root_impl) free((void *)environment_information32.root_impl);
  if (environment_information32.modules) {
    for (uint32_t i = 0; i < environment_information32.modules_len; i++) {
      free((void *)environment_information32.modules[i]);
    }
    free((void *)environment_information32.modules);
  }

  LOGI("Terminating ReZygisk monitor");
}

int send_control_command(enum rezygiskd_command cmd) {
  int sockfd = socket(PF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (sockfd == -1) return -1;

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX,
    .sun_path = { 0 }
  };

  size_t sun_path_len = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", rezygiskd_get_path(), SOCKET_NAME);

  socklen_t socklen = sizeof(sa_family_t) + sun_path_len;

  struct rzd_msg_header msg = {
    .cmd = cmd,
    .len = 0
  };
  ssize_t nsend = sendto(sockfd, (void *)&msg, sizeof(msg), 0, (struct sockaddr *)&addr, socklen);

  close(sockfd);

  return nsend != sizeof(struct rzd_msg_header) ? -1 : 0;
}
