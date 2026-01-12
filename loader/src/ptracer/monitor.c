#include <stdlib.h>

#include <time.h>

#include <sys/system_properties.h>
#include <sys/signalfd.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <fcntl.h>

#include <unistd.h>

#include "utils.h"
#include "daemon.h"
#include "misc.h"
#include "socket_utils.h"

#include "monitor.h"

#define PROP_PATH TMP_PATH "/module.prop"
#define SOCKET_NAME "init_monitor"

#define STOPPED_WITH(sig, event) WIFSTOPPED(sigchld_status) && (sigchld_status >> 8 == ((sig) | (event << 8)))

static bool update_status(const char *message);

char monitor_stop_reason[32];

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

bool monitor_events_unregister_event(int fd) {
  if (epoll_ctl(monitor_epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
    PLOGE("epoll_ctl");

    return false;
  }

  return true;
}

void monitor_events_stop() {
  monitor_events_running = false;
};

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

void rezygiskd_listener_callback() {
  while (1) {
    uint8_t cmd;
    ssize_t nread = TEMP_FAILURE_RETRY(read(monitor_sock_fd, &cmd, sizeof(cmd)));
    if (nread == -1) {
      PLOGE("read socket");

      continue;
    }

    switch (cmd) {
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
          strcpy(monitor_stop_reason, "user requested");

          ptrace(PTRACE_INTERRUPT, 1, 0, 0);
          update_status(NULL);
        }

        break;
      }
      case EXIT: {
        LOGI("Prepare for exit ...");

        tracing_state = EXITING;
        strcpy(monitor_stop_reason, "user requested");

        update_status(NULL);
        monitor_events_stop();

        break;
      }
      case ZYGOTE64_INJECTED: 
      case ZYGOTE32_INJECTED: {
        LOGI("Received Zygote%s injected command", cmd == ZYGOTE64_INJECTED ? "64" : "32");

        struct rezygiskd_status *status = cmd == ZYGOTE64_INJECTED ? &status64 : &status32;
        status->zygote_injected = true;

        update_status(NULL);

        break;
      }
      case DAEMON64_SET_INFO:
      case DAEMON32_SET_INFO: {
        LOGD("Received ReZygiskd%s info", cmd == DAEMON64_SET_INFO ? "64" : "32");

        uint32_t root_impl_len;
        if (read_uint32_t(monitor_sock_fd, &root_impl_len) != sizeof(root_impl_len)) {
          LOGE("read ReZygiskd%s root impl len", cmd == DAEMON64_SET_INFO ? "64" : "32");

          break;
        }

        struct environment_information *environment_information = cmd == DAEMON64_SET_INFO ? &environment_information64 : &environment_information32;
        if (environment_information->root_impl) {
          LOGD("freeing old ReZygiskd%s root impl", cmd == DAEMON64_SET_INFO ? "64" : "32");

          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;
        }

        environment_information->root_impl = malloc(root_impl_len + 1);
        if (environment_information->root_impl == NULL) {
          PLOGE("malloc ReZygiskd%s root impl", cmd == DAEMON64_SET_INFO ? "64" : "32");

          break;
        }

        if (read_loop(monitor_sock_fd, (void *)environment_information->root_impl, root_impl_len) != (ssize_t)root_impl_len) {
          LOGE("read ReZygiskd%s root impl", cmd == DAEMON64_SET_INFO ? "64" : "32");

          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;

          break;
        }

        environment_information->root_impl[root_impl_len] = '\0';
        LOGD("ReZygiskd%s root impl: %s", cmd == DAEMON64_SET_INFO ? "64" : "32", environment_information->root_impl);

        if (read_uint32_t(monitor_sock_fd, &environment_information->modules_len) != sizeof(environment_information->modules_len)) {
          LOGE("read ReZygiskd%s modules len", cmd == DAEMON64_SET_INFO ? "64" : "32");

          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;

          break;
        }

        if (environment_information->modules) {
          LOGD("freeing old ReZygiskd%s modules", cmd == DAEMON64_SET_INFO ? "64" : "32");

          for (size_t i = 0; i < environment_information->modules_len; i++) {
            free((void *)environment_information->modules[i]);
          }

          free((void *)environment_information->modules);
          environment_information->modules = NULL;
        }

        environment_information->modules = malloc(environment_information->modules_len * sizeof(char *));
        if (environment_information->modules == NULL) {
          PLOGE("malloc ReZygiskd%s modules", cmd == DAEMON64_SET_INFO ? "64" : "32");

          free((void *)environment_information->root_impl);
          environment_information->root_impl = NULL;

          break;
        }

        for (size_t i = 0; i < environment_information->modules_len; i++) {
          uint32_t module_name_len;
          if (read_uint32_t(monitor_sock_fd, &module_name_len) != sizeof(module_name_len)) {
            LOGE("read ReZygiskd%s module name len", cmd == DAEMON64_SET_INFO ? "64" : "32");

            goto rezygiskd64_set_info_modules_cleanup;
          }

          environment_information->modules[i] = malloc(module_name_len + 1);
          if (environment_information->modules[i] == NULL) {
            PLOGE("malloc ReZygiskd%s module name", cmd == DAEMON64_SET_INFO ? "64" : "32");

            goto rezygiskd64_set_info_modules_cleanup;
          }

          if (read_loop(monitor_sock_fd, (void *)environment_information->modules[i], module_name_len) != (ssize_t)module_name_len) {
            LOGE("read ReZygiskd%s module name", cmd == DAEMON64_SET_INFO ? "64" : "32");

            goto rezygiskd64_set_info_modules_cleanup;
          }

          environment_information->modules[i][module_name_len] = '\0';
          LOGD("ReZygiskd%s module %zu: %s", cmd == DAEMON64_SET_INFO ? "64" : "32", i, environment_information->modules[i]);

          continue;

          rezygiskd64_set_info_modules_cleanup:
            free((void *)environment_information->root_impl);
            environment_information->root_impl = NULL;

            for (size_t j = 0; j < i; j++) {
              free((void *)environment_information->modules[j]);
            }

            free((void *)environment_information->modules);
            environment_information->modules = NULL;

            break;
        }

        update_status(NULL);

        break;
      }
      case DAEMON64_SET_ERROR_INFO:
      case DAEMON32_SET_ERROR_INFO: {
        LOGD("Received ReZygiskd%s error info", cmd == DAEMON64_SET_ERROR_INFO ? "64" : "32");

        uint32_t error_info_len;
        if (read_uint32_t(monitor_sock_fd, &error_info_len) != sizeof(error_info_len)) {
          LOGE("read ReZygiskd%s error info len", cmd == DAEMON64_SET_ERROR_INFO ? "64" : "32");

          break;
        }

        struct rezygiskd_status *status = cmd == DAEMON64_SET_ERROR_INFO ? &status64 : &status32;
        if (status->daemon_error_info) {
          LOGD("freeing old ReZygiskd%s error info", cmd == DAEMON64_SET_ERROR_INFO ? "64" : "32");

          free(status->daemon_error_info);
          status->daemon_error_info = NULL;
        }

        status->daemon_error_info = malloc(error_info_len + 1);
        if (status->daemon_error_info == NULL) {
          PLOGE("malloc ReZygiskd%s error info", cmd == DAEMON64_SET_ERROR_INFO ? "64" : "32");

          break;
        }

        if (read_loop(monitor_sock_fd, status->daemon_error_info, error_info_len) != (ssize_t)error_info_len) {
          LOGE("read ReZygiskd%s error info", cmd == DAEMON64_SET_ERROR_INFO ? "64" : "32");

          free(status->daemon_error_info);
          status->daemon_error_info = NULL;

          break;
        }

        status->daemon_error_info[error_info_len] = '\0';
        LOGD("ReZygiskd%s error info: %s", cmd == DAEMON64_SET_ERROR_INFO ? "64" : "32", status->daemon_error_info);

        update_status(NULL);

        break;
      }
      case SYSTEM_SERVER_STARTED: {
        LOGD("system server started, mounting prop");

        if (mount(PROP_PATH, "/data/adb/modules/rezygisk/module.prop", NULL, MS_BIND, NULL) == -1) {
          PLOGE("failed to mount prop");
        }

        break;
      }
    }

    break;
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

  if (is_64bit || (!is_64bit && !status64.supported)) {
    LOGD("new zygote started.");

    umount2("/data/adb/modules/rezygisk/module.prop", MNT_DETACH);
  }

  if (status->daemon_pid != -1) {
    LOGI("daemon%s already running", is_64bit ? "64" : "32");

    return status->daemon_running;
  }

  pid_t pid = fork();
  if (pid < 0) {
    PLOGE("create daemon%s", is_64bit ? "64" : "32");

    return false;
  }

  if (pid == 0) {
    char daemon_name[PATH_MAX] = "./bin/zygiskd";
    strcat(daemon_name, is_64bit ? "64" : "32");

    execl(daemon_name, daemon_name, NULL);

    PLOGE("exec daemon %s failed", daemon_name);

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
    continue;                                                     \
  }

#define PRE_INJECT(abi, is_64)                                        \
  if (strcmp(program, "/system/bin/app_process" # abi) == 0) {        \
    tracer = "./bin/zygisk-ptrace" # abi;                             \
                                                                      \
    if (should_stop_inject ## abi()) {                                \
      LOGW("Zygote" # abi " restart too much times, stop injecting"); \
                                                                      \
      tracing_state = STOPPING;                                       \
      strcpy(monitor_stop_reason, "Zygote crashed");                  \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                              \
                                                                      \
      break;                                                          \
    }                                                                 \
                                                                      \
    if (!ensure_daemon_created(is_64)) {                              \
      LOGW("ReZygiskd " #abi "-bit not running, stop injecting");     \
                                                                      \
      tracing_state = STOPPING;                                       \
      strcpy(monitor_stop_reason, "ReZygiskd not running");           \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                              \
                                                                      \
      break;                                                          \
    }                                                                 \
  }

int sigchld_signal_fd;
struct signalfd_siginfo sigchld_fdsi;
int sigchld_status;

pid_t *sigchld_process;
size_t sigchld_process_count = 0;

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

  ptrace(PTRACE_SEIZE, 1, 0, PTRACE_O_TRACEFORK);

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
      LOGW("read %zu != %zu", s, sizeof(sigchld_fdsi));

      continue;
    }

    if (sigchld_fdsi.ssi_signo != SIGCHLD) {
      LOGW("no sigchld received");

      continue;
    }

    int pid;
    while ((pid = waitpid(-1, &sigchld_status, __WALL | WNOHANG)) != 0) {
      if (pid == -1) {
        if (tracing_state == STOPPED && errno == ECHILD) break;
        PLOGE("waitpid");
      }

      if (pid == 1) {
        if (STOPPED_WITH(SIGTRAP, PTRACE_EVENT_FORK)) {
          long child_pid;

          ptrace(PTRACE_GETEVENTMSG, pid, 0, &child_pid);

          LOGV("forked %ld", child_pid);
        } else if (STOPPED_WITH(SIGTRAP, PTRACE_EVENT_STOP) && tracing_state == STOPPING) {
          if (ptrace(PTRACE_DETACH, 1, 0, 0) == -1) PLOGE("failed to detach init");

          tracing_state = STOPPED;

          LOGI("stop tracing init");

          continue;
        }

        if (WIFSTOPPED(sigchld_status)) {
          if (WPTEVENT(sigchld_status) == 0) {
            if (WSTOPSIG(sigchld_status) != SIGSTOP && WSTOPSIG(sigchld_status) != SIGTSTP && WSTOPSIG(sigchld_status) != SIGTTIN && WSTOPSIG(sigchld_status) != SIGTTOU) {
              LOGW("inject signal sent to init: %s %d", sigabbrev_np(WSTOPSIG(sigchld_status)), WSTOPSIG(sigchld_status));

              ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(sigchld_status));

              continue;
            } else {
              LOGW("suppress stopping signal sent to init: %s %d", sigabbrev_np(WSTOPSIG(sigchld_status)), WSTOPSIG(sigchld_status));
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
        LOGV("new process %d attached", pid);

        for (size_t i = 0; i < sigchld_process_count; i++) {
          if (sigchld_process[i] != 0) continue;

          sigchld_process[i] = pid;

          goto ptrace_process;
        }

        sigchld_process = (pid_t *)realloc(sigchld_process, sizeof(pid_t) * (sigchld_process_count + 1));
        if (sigchld_process == NULL) {
          PLOGE("realloc sigchld_process");

          continue;
        }

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
          const char* tracer = NULL;

          do {
            if (tracing_state != TRACING) {
              LOGW("stop injecting %d because not tracing", pid);

              break;
            }

            PRE_INJECT(64, true)
            PRE_INJECT(32, false)

            if (tracer != NULL) {
              LOGD("stopping %d", pid);

              kill(pid, SIGSTOP);
              ptrace(PTRACE_CONT, pid, 0, 0);
              waitpid(pid, &sigchld_status, __WALL);

              if (STOPPED_WITH(SIGSTOP, 0)) {
                LOGD("detaching %d", pid);

                ptrace(PTRACE_DETACH, pid, 0, SIGSTOP);
                sigchld_status = 0;
                int p = fork_dont_care();

                if (p == 0) {
                  char pid_str[32];
                  sprintf(pid_str, "%d", pid);

                  execl(tracer, basename(tracer), "trace", pid_str, "--restart", NULL);

                  PLOGE("failed to exec, kill");

                  kill(pid, SIGKILL);
                  exit(1);
                } else if (p == -1) {
                  PLOGE("failed to fork, kill");

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

#define WRITE_STATUS_ABI(suffix)                                                     \
  if (status ## suffix.supported) {                                                  \
    strcat(status_text, ", ReZygisk " # suffix "-bit: ");                            \
                                                                                     \
    if (tracing_state != TRACING) strcat(status_text, "❌");                         \
    else if (status ## suffix.zygote_injected && status ## suffix.daemon_running)    \
      strcat(status_text, "✅");                                                     \
    else strcat(status_text, "⚠️");                                                  \
                                                                                     \
    if (!status ## suffix.daemon_running) {                                          \
      if (status ## suffix.daemon_error_info) {                                      \
        strcat(status_text, "(ReZygiskd: ");                                         \
        strcat(status_text, status ## suffix.daemon_error_info);                     \
        strcat(status_text, ")");                                                    \
      } else {                                                                       \
        strcat(status_text, "(ReZygiskd: not running)");                             \
      }                                                                              \
    }                                                                                \
  }

static bool update_status(const char *message) {
  FILE *prop = fopen(PROP_PATH, "w");
  if (prop == NULL) {
    PLOGE("failed to open prop");

    return false;
  }

  if (message) {
    fprintf(prop, "%s[%s] %s", pre_section, message, post_section);
    fclose(prop);

    return true;
  }

  char status_text[256] = "Monitor: ";
  switch (tracing_state) {
    case TRACING: {
      strcat(status_text, "✅");

      break;
    }
    case STOPPING: [[fallthrough]];
    case STOPPED: {
      strcat(status_text, "⛔");

      break;
    }
    case EXITING: {
      strcat(status_text, "❌");

      break;
    }
  }

  WRITE_STATUS_ABI(64)
  WRITE_STATUS_ABI(32)

  fprintf(prop, "%s[%s] %s", pre_section, status_text, post_section);
  fclose(prop);

  if (environment_information64.root_impl || environment_information32.root_impl) {
    FILE *json = fopen("/data/adb/rezygisk/state.json", "w");
    if (json == NULL) {
      PLOGE("failed to open state.json");

      return false;
    }

    fprintf(json, "{\n");
    fprintf(json, "  \"root\": \"%s\",\n", environment_information64.root_impl ? environment_information64.root_impl : environment_information32.root_impl);

    fprintf(json, "  \"monitor\": {\n");
    fprintf(json, "    \"state\": \"%d\"", tracing_state);
    if (monitor_stop_reason[0] != '\0') fprintf(json, ",\n    \"reason\": \"%s\"\n", monitor_stop_reason);
    else fprintf(json, "\n");
    fprintf(json, "  },\n");


    fprintf(json, "  \"rezygiskd\": {\n");
    if (status64.supported) {
      fprintf(json, "    \"64\": {\n");
      fprintf(json, "      \"state\": %d,\n", status64.daemon_running);
      if (status64.daemon_error_info) fprintf(json, "      \"reason\": \"%s\",\n", status64.daemon_error_info);
      fprintf(json, "      \"modules\": [");

      if (environment_information64.modules) for (uint32_t i = 0; i < environment_information64.modules_len; i++) {
        if (i > 0) fprintf(json, ", ");
        fprintf(json, "\"%s\"", environment_information64.modules[i]);
      }

      fprintf(json, "]\n");
      fprintf(json, "    }");
      if (status32.supported) fprintf(json, ",\n");
      else fprintf(json, "\n");
    }

    if (status32.supported) {
      fprintf(json, "    \"32\": {\n");
      fprintf(json, "      \"state\": %d,\n", status32.daemon_running);
      if (status32.daemon_error_info) fprintf(json, "      \"reason\": \"%s\",\n", status32.daemon_error_info);
      fprintf(json, "      \"modules\": [");

      if (environment_information32.modules) for (uint32_t i = 0; i < environment_information32.modules_len; i++) {
        if (i > 0) fprintf(json, ", ");
        fprintf(json, "\"%s\"", environment_information32.modules[i]);
      }

      fprintf(json, "]\n");
      fprintf(json, "    }\n");
    }

    fprintf(json, "  },\n");

    fprintf(json, "  \"zygote\": {\n");
    if (status64.supported) {
      fprintf(json, "    \"64\": %d", status64.zygote_injected);
      if (status32.zygote_injected) fprintf(json, ",\n");
      else fprintf(json, "\n");
    }
    if (status32.zygote_injected) {
      fprintf(json, "    \"32\": %d\n", status32.zygote_injected);
    }
    fprintf(json, "  }\n");
    fprintf(json, "}\n");

    fclose(json);
  } else {
    if (remove("/data/adb/rezygisk/state.json") == -1) {
      PLOGE("failed to remove state.json");
    }
  }

  LOGI("status updated: %s", status_text);

  return true;
}

static bool prepare_environment() {
  /* INFO: We need to create the file first, otherwise the mount will fail */
  close(open(PROP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644));

  FILE *orig_prop = fopen("/data/adb/modules/rezygisk/module.prop", "r");
  if (orig_prop == NULL) {
    PLOGE("failed to open orig prop");

    return false;
  }

  bool after_description = false;

  char line[1024];
  while (fgets(line, sizeof(line), orig_prop) != NULL) {
    if (strncmp(line, "description=", strlen("description=")) == 0) {
      strcat(pre_section, "description=");
      strcat(post_section, line + strlen("description="));
      after_description = true;

      continue;
    }

    if (after_description) strcat(post_section, line);
    else strcat(pre_section, line);
  }

  fclose(orig_prop);

  /* INFO: This environment variable is related to Magisk Zygisk/Manager. It
             it used by Magisk's Zygisk to communicate to Magisk Manager whether
             Zygisk is working or not.

           Because of that behavior, we can knowledge built-in Zygisk is being
             used and stop the continuation of initialization of ReZygisk.*/
  if (getenv("ZYGISK_ENABLED")) {
    update_status("❌ Disable Magisk's built-in Zygisk");

    return false;
  }

  return true;
}

void init_monitor() {
  LOGI("ReZygisk %s", ZKSU_VERSION);

  if (!prepare_environment()) exit(1);

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

  uint8_t cmd_op = cmd;
  ssize_t nsend = sendto(sockfd, (void *)&cmd_op, sizeof(cmd_op), 0, (struct sockaddr *)&addr, socklen);

  close(sockfd);

  return nsend != sizeof(cmd_op) ? -1 : 0;
}
