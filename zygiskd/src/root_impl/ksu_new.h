#ifndef __KSU_H_SUPERCALLS
#define __KSU_H_SUPERCALLS

struct ksu_get_info_cmd {
        uint32_t version; 
        uint32_t flags;
        uint32_t features;
};

struct ksu_uid_granted_root_cmd {
        uint32_t uid; 
        uint8_t granted; 
};

struct ksu_uid_should_umount_cmd {
        uint32_t uid; 
        uint8_t should_umount;
};

struct ksu_get_manager_uid_cmd {
        uint32_t uid;
};

#define KSU_IOCTL_GET_INFO _IOC(_IOC_READ, 'K', 2, 0)
#define KSU_IOCTL_UID_GRANTED_ROOT _IOC(_IOC_READ | _IOC_WRITE, 'K', 8, 0)
#define KSU_IOCTL_UID_SHOULD_UMOUNT _IOC(_IOC_READ | _IOC_WRITE, 'K', 9, 0)
#define KSU_IOCTL_GET_MANAGER_UID _IOC(_IOC_READ, 'K', 10, 0)

#define KSU_INSTALL_MAGIC1 0xDEADBEEF
#define KSU_INSTALL_MAGIC2 0xCAFEBABE

#endif // __KSU_H_SUPERCALLS
