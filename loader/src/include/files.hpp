#include <dirent.h>
#include <functional>
#include <string>
#include <vector>
#include <unistd.h>

struct mount_info {
    unsigned int id;
    unsigned int parent;
    dev_t device;
    std::string root;
    std::string target;
    std::string vfs_option;
    struct {
        unsigned int shared;
        unsigned int master;
        unsigned int propagate_from;
    } optional;
    std::string type;
    std::string source;
    std::string fs_option;
};

std::vector<mount_info> parse_mount_info(const char *pid);
