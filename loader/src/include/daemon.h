#pragma once

#include <string_view>
#include <string>
#include <unistd.h>
#include <vector>

#if defined(__LP64__)
# define LP_SELECT(lp32, lp64) lp64
#else
# define LP_SELECT(lp32, lp64) lp32
#endif

constexpr auto kCPSocketName = "/" LP_SELECT("cp32", "cp64") ".sock";

class UniqueFd {
    using Fd = int;
public:
    UniqueFd() = default;

    UniqueFd(Fd fd) : fd_(fd) {}

    ~UniqueFd() { if (fd_ >= 0) close(fd_); }

    // Disallow copy
    UniqueFd(const UniqueFd&) = delete;

    UniqueFd& operator=(const UniqueFd&) = delete;

    // Allow move
    UniqueFd(UniqueFd&& other) { std::swap(fd_, other.fd_); }

    UniqueFd& operator=(UniqueFd&& other) {
        std::swap(fd_, other.fd_);
        return *this;
    }

    // Implict cast to Fd
    operator const Fd&() const { return fd_; }

private:
    Fd fd_ = -1;
};

struct zygote_modules {
  char **modules;
  size_t modules_count;
};

enum zygote_root_impl {
  ZYGOTE_ROOT_IMPL_NONE,
  ZYGOTE_ROOT_IMPL_APATCH,
  ZYGOTE_ROOT_IMPL_KERNELSU,
  ZYGOTE_ROOT_IMPL_MAGISK
};

struct zygote_info {
  struct zygote_modules *modules;
  enum zygote_root_impl root_impl;
  pid_t pid;
  bool running;
};

enum mount_namespace_state {
    Clean,
    Rooted,
    Module
};

namespace zygiskd {

    struct ModuleInfo {
        std::string name;
        UniqueFd memfd;

        inline explicit ModuleInfo(std::string name, int memfd) : name(name), memfd(memfd) {}
    };

    enum class SocketAction {
        PingHeartBeat,
        GetProcessFlags,
        GetInfo,
        ReadModules,
        RequestCompanionSocket,
        GetModuleDir,
        ZygoteRestart,
        SystemServerStarted,
        UpdateMountNamespace
    };

    void Init(const char *path);

    std::string GetTmpPath();

    bool PingHeartbeat();

    std::vector<ModuleInfo> ReadModules();

    uint32_t GetProcessFlags(uid_t uid);

    int ConnectCompanion(size_t index);

    int GetModuleDir(size_t index);

    void ZygoteRestart();

    void SystemServerStarted();

    void GetInfo(struct zygote_info *info);

    std::string UpdateMountNamespace(enum mount_namespace_state mns_state);
}
