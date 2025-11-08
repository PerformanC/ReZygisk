#include <stdlib.h>

#include <errno.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../constants.h"
#include "../utils.h"
#include "common.h"
#include "kernelsu.h"
#include "ksu_new.h"

/* INFO: It would be presumed it is a unsigned int,
	   so we need to cast it to signed int to
	   avoid any potential UB.
*/
#define KERNEL_SU_OPTION (int)0xdeadbeef

#define CMD_GET_VERSION 2
#define CMD_UID_GRANTED_ROOT 12
#define CMD_UID_SHOULD_UMOUNT 13
#define CMD_GET_MANAGER_UID 16
#define CMD_HOOK_MODE 0xC0DEAD1A

static enum kernelsu_variants variant = KOfficial;

static bool supports_manager_uid_retrieval = false;

// new supercall

int global_fd = 0;
bool new_interface = true;

static void get_ksu_fd(void) 
{
	if (!!global_fd)
		return;

	syscall(SYS_reboot, KSU_INSTALL_MAGIC1, KSU_INSTALL_MAGIC2, 0, (void *)&global_fd);
	return;
}

static void set_interface(void) 
{
	static bool already_tested = false;

	if (already_tested)
		return;

	get_ksu_fd();
	if (!global_fd)
		new_interface = false;

	already_tested = true;
	return;
}

void ksu_get_existence(struct root_impl_state* state)
{
	int reply_ok = 0;

	int version = 0;

	// uint32_t version = 0;
	struct ksu_get_info_cmd info = { 0 };
	
	set_interface();

	if (new_interface) {
		syscall(SYS_ioctl, global_fd, KSU_IOCTL_GET_INFO, &info);	
		version = (int)info.version;
		goto skip_prctl;
	}

	prctl((signed int)KERNEL_SU_OPTION, CMD_GET_VERSION, &version, 0,
	      &reply_ok);
skip_prctl:
	if (version == 0)
		state->state = Abnormal;
	else if (version >= MIN_KSU_VERSION && version <= MAX_KSU_VERSION) {
		/* INFO: Some custom kernels for custom ROMs have pre-installed
		   KernelSU. Some users don't want to use KernelSU, but, for
		   example, Magisk. This if allows this to happen, as it checks
		   if "ksud" exists, which in case it doesn't, it won't be
		   considered as supported. */
		struct stat s;
		if (stat("/data/adb/ksud", &s) == -1) {
			if (errno != ENOENT) {
			        LOGE("Failed to stat KSU daemon: %s\n",
			             strerror(errno));
			}
			errno = 0;
			state->state = Abnormal;

			return;
		}

		state->state = Supported;

		char mode[16] = {0};
		prctl((signed int)KERNEL_SU_OPTION, CMD_HOOK_MODE, mode, NULL,
		      &reply_ok);

		if (mode[0] != '\0')
			state->variant = KNext;
		else
			state->variant = KOfficial;

		variant = state->variant;

		/* INFO: CMD_GET_MANAGER_UID is a KernelSU Next feature, however
		   we won't limit to KernelSU Next only in case other forks wish
		   to implement it. */
		prctl((signed int)KERNEL_SU_OPTION, CMD_GET_MANAGER_UID, NULL,
		      NULL, &reply_ok);

		if (reply_ok == KERNEL_SU_OPTION) {
			LOGI(
			    "KernelSU implementation supports "
			    "CMD_GET_MANAGER_UID.\n");

			supports_manager_uid_retrieval = true;
		}
	} else if (version >= 1 && version <= MIN_KSU_VERSION - 1)
		state->state = TooOld;
	else
		state->state = Abnormal;
}

bool ksu_uid_granted_root(uid_t uid)
{
	uint32_t result = 0;
	bool granted = false;

	struct ksu_uid_granted_root_cmd data = { 0 };

	if (new_interface) {
		data.uid = uid;		
		syscall(SYS_ioctl, global_fd, KSU_IOCTL_UID_GRANTED_ROOT, &data);	
		return !!data.granted;
	}

	prctl(KERNEL_SU_OPTION, CMD_UID_GRANTED_ROOT, uid, &granted, &result);

	if ((int)result != KERNEL_SU_OPTION) return false;

	return granted;
}

bool ksu_uid_should_umount(uid_t uid)
{
	uint32_t result = 0;
	bool umount = false;

	struct ksu_uid_should_umount_cmd data = { 0 };

	if (new_interface) {
		data.uid = uid;
		syscall(SYS_ioctl, global_fd, KSU_IOCTL_UID_SHOULD_UMOUNT, &data);	
		return !!data.should_umount;
	}

	prctl(KERNEL_SU_OPTION, CMD_UID_SHOULD_UMOUNT, uid, &umount, &result);

	if ((int)result != KERNEL_SU_OPTION) return false;

	return umount;
}

bool ksu_uid_is_manager(uid_t uid)
{

	/* new interface always supports this */
	struct ksu_get_manager_uid_cmd data = { 0 };

	if (new_interface) {
		syscall(SYS_ioctl, global_fd, KSU_IOCTL_GET_MANAGER_UID, &data);
		return data.uid == uid;
	}

	/* INFO: If the manager UID is set, we can use it to check if the UID
		   is the manager UID, which is more reliable than checking
		   the KSU manager data directory, as spoofed builds of
		   KernelSU Next have different package names.
	*/
	if (supports_manager_uid_retrieval) {
		int reply_ok = 0;

		uid_t manager_uid = 0;
		prctl(KERNEL_SU_OPTION, CMD_GET_MANAGER_UID, &manager_uid, NULL,
		      &reply_ok);

		return uid == manager_uid;
	}

	const char* manager_path = NULL;
	if (variant == KOfficial)
		manager_path = "/data/user_de/0/me.weishu.kernelsu";
	else if (variant == KNext)
		manager_path = "/data/user_de/0/com.rifsxd.ksunext";

	struct stat s;
	if (stat(manager_path, &s) == -1) {
		if (errno != ENOENT) {
			LOGE("Failed to stat KSU manager data directory: %s\n",
			     strerror(errno));
		}
		errno = 0;

		return false;
	}

	return s.st_uid == uid;
}
