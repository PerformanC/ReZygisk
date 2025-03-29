#include <stdlib.h>

#include <sys/system_properties.h>
#include <unistd.h>
#include <set>
#include <sys/signalfd.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <fcntl.h>

#include "monitor.h"
#include "utils.hpp"
#include "files.hpp"
#include "misc.hpp"

#define STOPPED_WITH(sig, event) WIFSTOPPED(status) && (status >> 8 == ((sig) | (event << 8)))

static void updateStatus();

char monitor_stop_reason[32];

constexpr char SOCKET_NAME[] = "init_monitor";

struct EventLoop;

struct EventHandler {
    virtual int GetFd() = 0;
    virtual void HandleEvent(EventLoop& loop, uint32_t event) = 0;
};

struct EventLoop {
  private:
    int epoll_fd_;
    bool running = false;
  public:
    bool Init() {
      epoll_fd_ = epoll_create(1);
      if (epoll_fd_ == -1) {
        PLOGE("failed to create");

        return false;
      }

      return true;
    }

    void Stop() {
      running = false;
    }

    void Loop() {
      running = true;

      constexpr auto MAX_EVENTS = 2;
      struct epoll_event events[MAX_EVENTS];

      while (running) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds == -1) {
          if (errno != EINTR) PLOGE("epoll_wait");

          continue;
        }

        for (int i = 0; i < nfds; i++) {
          reinterpret_cast<EventHandler *>(events[i].data.ptr)->HandleEvent(*this,
                                                                            events[i].events);
          if (!running) break;
        }
      }
    }

    bool RegisterHandler(EventHandler &handler, uint32_t events) {
      struct epoll_event ev{};
      ev.events = events;
      ev.data.ptr = &handler;
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, handler.GetFd(), &ev) == -1) {
        PLOGE("failed to add event handler");

        return false;
      }

      return true;
    }

    bool UnregisterHandler(EventHandler &handler) {
      if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, handler.GetFd(), NULL) == -1) {
        PLOGE("failed to del event handler");

        return false;
      }

      return true;
    }

    ~EventLoop() {
      if (epoll_fd_ >= 0) close(epoll_fd_);
    }
};

enum TracingState {
  TRACING = 1,
  STOPPING,
  STOPPED,
  EXITING
};

TracingState tracing_state = TRACING;
static char prop_path[PATH_MAX];

struct Status {
  bool supported = false;
  bool zygote_injected = false;
  bool daemon_running = false;
  pid_t daemon_pid = -1;
  char *daemon_info = NULL;
  char *daemon_error_info = NULL;
};

Status status64;
Status status32;

struct SocketHandler : public EventHandler {
  int sock_fd_;

  bool Init() {
    sock_fd_ = socket(PF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (sock_fd_ == -1) {
      PLOGE("socket create");

      return false;
    }

    struct sockaddr_un addr = {
      .sun_family = AF_UNIX,
      .sun_path = { 0 }
    };

    size_t sun_path_len = sprintf(addr.sun_path, "%s/%s", zygiskd::GetTmpPath().c_str(), SOCKET_NAME);

    socklen_t socklen = sizeof(sa_family_t) + sun_path_len;
    if (bind(sock_fd_, (struct sockaddr *)&addr, socklen) == -1) {
      PLOGE("bind socket");

      return false;
    }

    return true;
  }

  int GetFd() override {
    return sock_fd_;
  }

  void HandleEvent(EventLoop &loop, uint32_t) override {
    struct [[gnu::packed]] MsgHead {
      enum Command cmd;
      int length;
      char data[0];
    };

    while (1) {
      struct MsgHead *msg = (struct MsgHead *)malloc(sizeof(struct MsgHead));

      ssize_t real_size;
      ssize_t nread = recv(sock_fd_, msg, sizeof(struct MsgHead), MSG_PEEK);
      if (nread == -1) {
        if (errno == EAGAIN) break;

        PLOGE("read socket");
      }

      if ((size_t)nread < sizeof(Command)) {
        LOGE("read %zu < %zu", nread, sizeof(Command));
        continue;
      }

      if (msg->cmd >= Command::DAEMON64_SET_INFO && msg->cmd != Command::SYSTEM_SERVER_STARTED) {
        if (nread != sizeof(msg)) {
          LOGE("cmd %d size %zu != %zu", msg->cmd, nread, sizeof(MsgHead));

          continue;
        }

        real_size = sizeof(MsgHead) + msg->length;
      } else {
        if (nread != sizeof(Command)) {
          LOGE("cmd %d size %zu != %zu", msg->cmd, nread, sizeof(Command));

          continue;
        }

        real_size = sizeof(Command);
      }

      msg = (struct MsgHead *)realloc(msg, real_size);
      nread = recv(sock_fd_, msg, real_size, 0);

      if (nread == -1) {
        if (errno == EAGAIN) break;

        PLOGE("recv");
        continue;
      }

      if (nread != real_size) {
        LOGE("real size %zu != %zu", real_size, nread);

        continue;
      }

      switch (msg->cmd) {
        case START: {
          if (tracing_state == STOPPING) tracing_state = TRACING;
          else if (tracing_state == STOPPED) {
            ptrace(PTRACE_SEIZE, 1, 0, PTRACE_O_TRACEFORK);

            LOGI("start tracing init");

            tracing_state = TRACING;
          }

          updateStatus();

          break;
        }
        case STOP: {
          if (tracing_state == TRACING) {
            LOGI("stop tracing requested");

            tracing_state = STOPPING;
            strcpy(monitor_stop_reason, "user requested");

            ptrace(PTRACE_INTERRUPT, 1, 0, 0);
            updateStatus();
          }

          break;
        }
        case EXIT: {
          LOGI("prepare for exit ...");

          tracing_state = EXITING;
          strcpy(monitor_stop_reason, "user requested");

          updateStatus();
          loop.Stop();

          break;
        }
        case ZYGOTE64_INJECTED: {
          status64.zygote_injected = true;

          updateStatus();

          break;
        }
        case ZYGOTE32_INJECTED: {
          status32.zygote_injected = true;

          updateStatus();

          break;
        }
        case DAEMON64_SET_INFO: {
          LOGD("received daemon64 info %s", msg->data);

          /* Will only happen if somehow the daemon restarts */
          if (status64.daemon_info) {
            free(status64.daemon_info);
            status64.daemon_info = NULL;
          }

          status64.daemon_info = (char *)malloc(msg->length);
          if (!status64.daemon_info) {
            PLOGE("malloc daemon64 info");

            break;
          }

          strcpy(status64.daemon_info, msg->data);

          updateStatus();

          break;
        }
        case DAEMON32_SET_INFO: {
          LOGD("received daemon32 info %s", msg->data);

          if (status32.daemon_info) {
            free(status32.daemon_info);
            status32.daemon_info = NULL;
          }

          status32.daemon_info = (char *)malloc(msg->length);
          if (!status32.daemon_info) {
            PLOGE("malloc daemon32 info");

            break;
          }

          strcpy(status32.daemon_info, msg->data);

          updateStatus();

          break;
        }
        case DAEMON64_SET_ERROR_INFO: {
          LOGD("received daemon64 error info %s", msg->data);

          status64.daemon_running = false;

          if (status64.daemon_error_info) {
            free(status64.daemon_error_info);
            status64.daemon_error_info = NULL;
          }

          status64.daemon_error_info = (char *)malloc(msg->length);
          if (!status64.daemon_error_info) {
            PLOGE("malloc daemon64 error info");

            break;
          }

          strcpy(status64.daemon_error_info, msg->data);

          updateStatus();

          break;
        }
        case DAEMON32_SET_ERROR_INFO: {
          LOGD("received daemon32 error info %s", msg->data);

          status32.daemon_running = false;

          if (status32.daemon_error_info) {
            free(status32.daemon_error_info);
            status32.daemon_error_info = NULL;
          }
          
          status32.daemon_error_info = (char *)malloc(msg->length);
          if (!status32.daemon_error_info) {
            PLOGE("malloc daemon32 error info");

            break;
          }

          strcpy(status32.daemon_error_info, msg->data);

          updateStatus();

          break;
        }
        case SYSTEM_SERVER_STARTED: {
          LOGD("system server started, mounting prop");

          if (mount(prop_path, "/data/adb/modules/zygisksu/module.prop", NULL, MS_BIND, NULL) == -1) {
            PLOGE("failed to mount prop");
          }

          break;
        }
      }

      free(msg);
    }
  }

  ~SocketHandler() {
    if (sock_fd_ >= 0) close(sock_fd_);
  }
};

constexpr int MAX_RETRY_COUNT = 5;

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
  Status *status = is_64bit ? &status64 : &status32;
  if (is_64bit) {
    LOGD("new zygote started.");

    umount2("/data/adb/modules/zygisksu/module.prop", MNT_DETACH);
  }

  status->zygote_injected = false;

  if (status->daemon_pid == -1) {
    pid_t pid = fork();
    if (pid < 0) {
      PLOGE("create daemon%s", is_64bit ? "64" : "32");

      return false;
    } else if (pid == 0) {
      char daemon_name[PATH_MAX] = "./bin/zygiskd";
      strcat(daemon_name, is_64bit ? "64" : "32");

      execl(daemon_name, daemon_name, NULL);

      PLOGE("exec daemon %s failed", daemon_name);

      exit(1);
    } else {
      status->supported = true;
      status->daemon_pid = pid;
      status->daemon_running = true;

      return true;
    }
  } else {
    return status->daemon_running;
  }
}

#define CHECK_DAEMON_EXIT(abi)                                                   \
  if (status##abi.supported && pid == status64.daemon_pid) {                     \
    char status_str[64];                                                         \
    parse_status(status, status_str, sizeof(status_str));                        \
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
    updateStatus();                                                              \
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

struct SigChldHandler : public EventHandler {
  private:
    int signal_fd_;
    struct signalfd_siginfo fdsi;
    int status;
    std::set<pid_t> process;

  public:
    bool Init() {
      sigset_t mask;
      sigemptyset(&mask);
      sigaddset(&mask, SIGCHLD);

      if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        PLOGE("set sigprocmask");

        return false;
      }

      signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
      if (signal_fd_ == -1) {
        PLOGE("create signalfd");

        return false;
      }

      ptrace(PTRACE_SEIZE, 1, 0, PTRACE_O_TRACEFORK);

      return true;
    }

    int GetFd() override {
      return signal_fd_;
    }

    void HandleEvent(EventLoop &, uint32_t) override {
      while (1) {
        ssize_t s = read(signal_fd_, &fdsi, sizeof(fdsi));
        if (s == -1) {
          if (errno == EAGAIN) break;

          PLOGE("read signalfd");

          continue;
        }

        if (s != sizeof(fdsi)) {
          LOGW("read %zu != %zu", s, sizeof(fdsi));

          continue;
        }

        if (fdsi.ssi_signo != SIGCHLD) {
          LOGW("no sigchld received");

          continue;
        }

        int pid;
        while ((pid = waitpid(-1, &status, __WALL | WNOHANG)) != 0) {
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

            if (WIFSTOPPED(status)) {
              if (WPTEVENT(status) == 0) {
                if (WSTOPSIG(status) != SIGSTOP && WSTOPSIG(status) != SIGTSTP && WSTOPSIG(status) != SIGTTIN && WSTOPSIG(status) != SIGTTOU) {
                  LOGW("inject signal sent to init: %s %d", sigabbrev_np(WSTOPSIG(status)), WSTOPSIG(status));

                  ptrace(PTRACE_CONT, pid, 0, WSTOPSIG(status));

                  continue;
                } else {
                  LOGW("suppress stopping signal sent to init: %s %d", sigabbrev_np(WSTOPSIG(status)), WSTOPSIG(status));
                }
              }

              ptrace(PTRACE_CONT, pid, 0, 0);
            }

            continue;
          }

          CHECK_DAEMON_EXIT(64)
          CHECK_DAEMON_EXIT(32)

          auto state = process.find(pid);

          if (state == process.end()) {
            LOGV("new process %d attached", pid);

            process.emplace(pid);

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
                  waitpid(pid, &status, __WALL);

                  if (STOPPED_WITH(SIGSTOP, 0)) {
                    LOGD("detaching %d", pid);

                    ptrace(PTRACE_DETACH, pid, 0, SIGSTOP);
                    status = 0;
                    auto p = fork_dont_care();

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

              updateStatus();
            } else {
              char status_str[64];
              parse_status(status, status_str, sizeof(status_str));

              LOGW("process %d received unknown status %s", pid, status_str);
            }

            process.erase(state);
            if (WIFSTOPPED(status)) {
              LOGV("detach process %d", pid);

              ptrace(PTRACE_DETACH, pid, 0, 0);
            }
          }
        }
      }
    }

    ~SigChldHandler() {
      if (signal_fd_ >= 0) close(signal_fd_);
    }
};

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

#define WRITE_STATUS_FILE(suffix)                                               \
  if (status ## suffix.supported) {                                             \
    fprintf(state_file, "Daemon" # suffix ": ");                                \
    if (status ## suffix.daemon_running) {                                      \
      fprintf(state_file, "running");                                           \
                                                                                \
      if (status ## suffix.daemon_info != NULL)                                 \
        fprintf(state_file, " (%s)", status ## suffix.daemon_info);             \
    } else {                                                                    \
      fprintf(state_file, "crashed");                                           \
                                                                                \
      if (status ## suffix.daemon_error_info != NULL)                           \
        fprintf(state_file, " (%s)", status ## suffix.daemon_error_info);       \
    }                                                                           \
                                                                                \
    fprintf(state_file, "\nZygote" # suffix ": ");                              \
    if (tracing_state != TRACING) fprintf(state_file, "unknown");               \
    else if (status ## suffix.zygote_injected) fprintf(state_file, "injected"); \
    else fprintf(state_file, "not injected");                                   \
                                                                                \
    fprintf(state_file, "\n");                                                  \
  } 

static void updateStatus() {
  FILE *prop = fopen(prop_path, "w");
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

  fprintf(prop, "%s[%s] %s", pre_section, status_text, post_section);

  fclose(prop);

  char state_file_path[PATH_MAX];
  snprintf(state_file_path, sizeof(state_file_path), "%s/%s", zygiskd::GetTmpPath().c_str(), "status");

  FILE *state_file = fopen(state_file_path, "w");
  if (state_file == NULL) {
    PLOGE("failed to open state file");

    return;
  }

  fprintf(state_file, "Version: %s\n", ZKSU_VERSION);
  fprintf(state_file, "Tracing: %d", tracing_state);
  if (tracing_state != TRACING && monitor_stop_reason[0] != '\0') {
    fprintf(state_file, " (%s)\n", monitor_stop_reason);
  } else {
    fprintf(state_file, "\n");
  }

  WRITE_STATUS_FILE(64)
  WRITE_STATUS_FILE(32)

  fclose(state_file);
}

static bool prepare_environment() {
  strcat(prop_path, zygiskd::GetTmpPath().c_str());
  strcat(prop_path, "/module.prop");

  close(open(prop_path, O_WRONLY | O_CREAT | O_TRUNC, 0644));

  FILE *orig_prop = fopen("./module.prop", "r");
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

  /* TODO: See if ZYGISK_ENABLED flag is already set,
             if so, set a status saying to disable built-in Zygisk. */
  updateStatus();

  return true;
}

void init_monitor() {
  LOGI("ReZygisk %s", ZKSU_VERSION);

  if (!prepare_environment()) exit(1);

  SocketHandler socketHandler{};
  socketHandler.Init();
  SigChldHandler ptraceHandler{};
  ptraceHandler.Init();
  EventLoop looper;

  looper.Init();
  looper.RegisterHandler(socketHandler, EPOLLIN | EPOLLET);
  looper.RegisterHandler(ptraceHandler, EPOLLIN | EPOLLET);
  looper.Loop();

  if (status64.daemon_info) free(status64.daemon_info);
  if (status64.daemon_error_info) free(status64.daemon_error_info);
  if (status32.daemon_info) free(status32.daemon_info);
  if (status32.daemon_error_info) free(status32.daemon_error_info);

  LOGI("exit");
}

int send_control_command(enum Command cmd) {
  int sockfd = socket(PF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (sockfd == -1) return -1;

  struct sockaddr_un addr = {
    .sun_family = AF_UNIX,
    .sun_path = { 0 }
  };

  size_t sun_path_len = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", zygiskd::GetTmpPath().c_str(), SOCKET_NAME);
  socklen_t socklen = sizeof(sa_family_t) + sun_path_len;

  ssize_t nsend = sendto(sockfd, (void *)&cmd, sizeof(cmd), 0, (sockaddr *)&addr, socklen);

  /* TODO: Should we close even when it fails? */
  close(sockfd);

  return nsend != sizeof(cmd) ? -1 : 0;
}
