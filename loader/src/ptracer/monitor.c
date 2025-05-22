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

#include "monitor.h"

#define PROP_PATH TMP_PATH "/module.prop"
#define SOCKET_NAME "init_monitor"

#define STOPPED_WITH(sig, event) WIFSTOPPED(sigchld_status) && (sigchld_status >> 8 == ((sig) | (event << 8)))

static bool update_status(const char *message);

char monitor_stop_reason[32];

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

bool monitor_events_init() {
  monitor_epoll_fd = epoll_create(1);
  if (monitor_epoll_fd == -1) {
    PLOGE("epoll_create");

    return false;
  }

  return true;
}

struct monitor_event_cbs {
  void (*callback)();
  void (*stop_callback)();
};

bool monitor_events_register_event(struct monitor_event_cbs *event_cbs, int fd, uint32_t events) {
  struct epoll_event ev = {
    .data.ptr = event_cbs,
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
    if (nfds == -1) {
      if (errno != EINTR) PLOGE("epoll_wait");

      continue;
    }

    for (int i = 0; i < nfds; i++) {
      struct monitor_event_cbs *event_cbs = (struct monitor_event_cbs *)events[i].data.ptr;
      event_cbs->callback();

      if (!monitor_events_running) break;
    }
  }

  if (monitor_epoll_fd >= 0) close(monitor_epoll_fd);
  monitor_epoll_fd = -1;

  for (int i = 0; i < (int)(sizeof(events) / sizeof(events[0])); i++) {
    struct monitor_event_cbs *event_cbs = (struct monitor_event_cbs *)events[i].data.ptr;
    event_cbs->stop_callback();
  }
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

struct __attribute__((__packed__)) MsgHead {
  unsigned int cmd;
  int length;
};

void rezygiskd_listener_callback() {
  while (1) {
    struct MsgHead msg = { 0 };

    size_t nread;

    again:
      nread = read(monitor_sock_fd, &msg, sizeof(msg));
      if ((int)nread == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) goto again;

        PLOGE("read socket");

        continue;
      }

    char *msg_data = NULL;

    if (msg.length != 0) {
      msg_data = malloc(msg.length);
      if (!msg_data) {
        LOGE("malloc msg data failed");

        continue;
      }

      again_msg_data:
        nread = read(monitor_sock_fd, msg_data, msg.length);
        if ((int)nread == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) goto again_msg_data;

          PLOGE("read socket");

          free(msg_data);

          continue;
        }
    }

    switch (msg.cmd) {
      case START: {
        if (tracing_state == STOPPING) tracing_state = TRACING;
        else if (tracing_state == STOPPED) {
          ptrace(PTRACE_SEIZE, 1, 0, PTRACE_O_TRACEFORK);

          LOGI("start tracing init");

          tracing_state = TRACING;
        }

        update_status(NULL);

        break;
      }
      case STOP: {
        if (tracing_state == TRACING) {
          LOGI("stop tracing requested");

          tracing_state = STOPPING;
          strcpy(monitor_stop_reason, "user requested");

          ptrace(PTRACE_INTERRUPT, 1, 0, 0);
          update_status(NULL);
        }

        break;
      }
      case EXIT: {
        LOGI("prepare for exit ...");

        tracing_state = EXITING;
        strcpy(monitor_stop_reason, "user requested");

        update_status(NULL);
        monitor_events_stop();

        break;
      }
      case ZYGOTE64_INJECTED: {
        status64.zygote_injected = true;

        update_status(NULL);

        break;
      }
      case ZYGOTE32_INJECTED: {
        status32.zygote_injected = true;

        update_status(NULL);

        break;
      }
      case DAEMON64_SET_INFO: {
        LOGD("received daemon64 info %s", msg_data);

        /* Will only happen if somehow the daemon restarts */
        if (status64.daemon_info) {
          free(status64.daemon_info);
          status64.daemon_info = NULL;
        }

        status64.daemon_info = (char *)malloc(msg.length);
        if (!status64.daemon_info) {
          PLOGE("malloc daemon64 info");

          break;
        }

        strcpy(status64.daemon_info, msg_data);

        update_status(NULL);

        break;
      }
      case DAEMON32_SET_INFO: {
        LOGD("received daemon32 info %s", msg_data);

        if (status32.daemon_info) {
          free(status32.daemon_info);
          status32.daemon_info = NULL;
        }

        status32.daemon_info = (char *)malloc(msg.length);
        if (!status32.daemon_info) {
          PLOGE("malloc daemon32 info");

          break;
        }

        strcpy(status32.daemon_info, msg_data);

        update_status(NULL);

        break;
      }
      case DAEMON64_SET_ERROR_INFO: {
        LOGD("received daemon64 error info %s", msg_data);

        status64.daemon_running = false;

        if (status64.daemon_error_info) {
          free(status64.daemon_error_info);
          status64.daemon_error_info = NULL;
        }

        status64.daemon_error_info = (char *)malloc(msg.length);
        if (!status64.daemon_error_info) {
          PLOGE("malloc daemon64 error info");

          break;
        }

        strcpy(status64.daemon_error_info, msg_data);

        update_status(NULL);

        break;
      }
      case DAEMON32_SET_ERROR_INFO: {
        LOGD("received daemon32 error info %s", msg_data);

        status32.daemon_running = false;

        if (status32.daemon_error_info) {
          free(status32.daemon_error_info);
          status32.daemon_error_info = NULL;
        }

        status32.daemon_error_info = (char *)malloc(msg.length);
        if (!status32.daemon_error_info) {
          PLOGE("malloc daemon32 error info");

          break;
        }

        strcpy(status32.daemon_error_info, msg_data);

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

    if (msg_data) free(msg_data);

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

#define CHECK_DAEMON_EXIT(abi)                                                   \
  if (status##abi.supported && pid == status##abi.daemon_pid) {                  \
    char status_str[64];                                                         \
    parse_status(sigchld_status, status_str, sizeof(status_str));                \
                                                                                 \
    LOGW("daemon" #abi " pid %d exited: %s", pid, status_str);                   \
    status##abi.daemon_running = false;                                          \
                                                                                 \
    if (!status##abi.daemon_error_info) {                                        \
      status##abi.daemon_error_info = (char *)malloc(strlen(status_str) + 1);    \
      if (!status##abi.daemon_error_info) {                                      \
        LOGE("malloc daemon" #abi " error info failed");                         \
                                                                                 \
        return;                                                                  \
      }                                                                          \
                                                                                 \
      memcpy(status##abi.daemon_error_info, status_str, strlen(status_str) + 1); \
    }                                                                            \
                                                                                 \
    update_status(NULL);                                                         \
    continue;                                                                    \
  }

#define PRE_INJECT(abi, is_64)                                                         \
  if (strcmp(program, "/system/bin/app_process" # abi) == 0) {                         \
    tracer = "./bin/zygisk-ptrace" # abi;                                              \
                                                                                       \
    if (should_stop_inject ## abi()) {                                                 \
      LOGW("zygote" # abi " restart too much times, stop injecting");                  \
                                                                                       \
      tracing_state = STOPPING;                                                        \
      memcpy(monitor_stop_reason, "zygote crashed", sizeof("zygote crashed"));         \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                                               \
                                                                                       \
      break;                                                                           \
    }                                                                                  \
    if (!ensure_daemon_created(is_64)) {                                               \
      LOGW("daemon" #abi " not running, stop injecting");                              \
                                                                                       \
      tracing_state = STOPPING;                                                        \
      memcpy(monitor_stop_reason, "daemon not running", sizeof("daemon not running")); \
      ptrace(PTRACE_INTERRUPT, 1, 0, 0);                                               \
                                                                                       \
      break;                                                                           \
    }                                                                                  \
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

          update_status(NULL);
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
    strcat(status_text, " zygote" # suffix ": ");                                    \
    if (tracing_state != TRACING) strcat(status_text, "❓ unknown, ");               \
    else if (status ## suffix.zygote_injected) strcat(status_text, "😋 injected, "); \
    else strcat(status_text, "❌ not injected, ");                                   \
                                                                                     \
    strcat(status_text, "daemon" # suffix ": ");                                     \
    if (status ## suffix.daemon_running) {                                           \
      strcat(status_text, "😋 running ");                                            \
                                                                                     \
      if (status ## suffix.daemon_info != NULL) {                                    \
        strcat(status_text, "(");                                                    \
        strcat(status_text, status ## suffix.daemon_info);                           \
        strcat(status_text, ")");                                                    \
      }                                                                              \
    } else {                                                                         \
      strcat(status_text, "❌ crashed ");                                            \
                                                                                     \
      if (status ## suffix.daemon_error_info != NULL) {                              \
        strcat(status_text, "(");                                                    \
        strcat(status_text, status ## suffix.daemon_error_info);                     \
        strcat(status_text, ")");                                                    \
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
    fprintf(prop, "%s[%s]\\n%s", pre_section, message, post_section);
    fclose(prop);

    return true;
  }

  char status_text[1024] = "monitor: ";

  switch (tracing_state) {
    case TRACING: {
      strcat(status_text, "😋 tracing");

      break;
    }
    case STOPPING: [[fallthrough]];
    case STOPPED: {
      strcat(status_text, "❌ stopped");

      break;
    }
    case EXITING: {
      strcat(status_text, "❌ exited");

      break;
    }
  }

  if (tracing_state != TRACING && monitor_stop_reason[0] != '\0') {
    strcat(status_text, " (");
    strcat(status_text, monitor_stop_reason);
    strcat(status_text, ")");
  }
  strcat(status_text, ",");

  WRITE_STATUS_ABI(64)
  WRITE_STATUS_ABI(32)

  fprintf(prop, "%s[%s]\\n%s", pre_section, status_text, post_section);

  fclose(prop);

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

  return update_status(NULL);
}

void init_monitor() {
  LOGI("ReZygisk %s", ZKSU_VERSION);

  if (!prepare_environment()) exit(1);

  monitor_events_init();

  struct monitor_event_cbs listener_cbs = {
    .callback = rezygiskd_listener_callback,
    .stop_callback = rezygiskd_listener_stop
  };
  if (!rezygiskd_listener_init()) {
    LOGE("failed to create socket");

    close(monitor_epoll_fd);

    exit(1);
  }

  monitor_events_register_event(&listener_cbs, monitor_sock_fd, EPOLLIN | EPOLLET);

  struct monitor_event_cbs sigchld_cbs = {
    .callback = sigchld_listener_callback,
    .stop_callback = sigchld_listener_stop
  };
  if (sigchld_listener_init() == false) {
    LOGE("failed to create signalfd");

    rezygiskd_listener_stop();
    close(monitor_epoll_fd);

    exit(1);
  }

  monitor_events_register_event(&sigchld_cbs, sigchld_signal_fd, EPOLLIN | EPOLLET);

  monitor_events_loop();

  if (status64.daemon_info) free(status64.daemon_info);
  if (status64.daemon_error_info) free(status64.daemon_error_info);
  if (status32.daemon_info) free(status32.daemon_info);
  if (status32.daemon_error_info) free(status32.daemon_error_info);

  LOGI("exit");
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

  ssize_t nsend = sendto(sockfd, (void *)&cmd, sizeof(cmd), 0, (struct sockaddr *)&addr, socklen);

  close(sockfd);

  return nsend != sizeof(cmd) ? -1 : 0;
}
