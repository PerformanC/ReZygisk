#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include <unistd.h>
#include <linux/limits.h>
#include <pthread.h>

#include <android/log.h>

#include "utils.h"

#undef LOG_TAG
#define LOG_TAG lp_select("zygiskd-companion32", "zygiskd-companion64")

typedef void (*zygisk_companion_entry)(int);

struct companion_module_thread_args {
  int fd;
  zygisk_companion_entry entry;
};

zygisk_companion_entry load_module(int fd) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

  void *handle = dlopen(path, RTLD_NOW);
  if (!handle) {
    LOGE("Failed to dlopen module: %s\n", dlerror());

    return NULL;
  }

  void *entry = dlsym(handle, "zygisk_companion_entry");
  if (!entry) {
    LOGE("Failed to dlsym zygisk_companion_entry: %s\n", dlerror());

    dlclose(handle);

    return NULL;
  }

  return (zygisk_companion_entry)entry;
}

/* WARNING: Dynamic memory based */
void *entry_thread(void *arg) {
  struct companion_module_thread_args *args = (struct companion_module_thread_args *)arg;

  int fd = args->fd;
  zygisk_companion_entry module_entry = args->entry;

  struct stat st0 = { 0 };
  if (fstat(fd, &st0) == -1) {
    LOGE(" - Failed to get initial client fd stats: %s\n", strerror(errno));

    free(args);

    return NULL;
  }

  module_entry(fd);

  /* INFO: Only attempt to close the client fd if it appears to be the same file
             and if we can successfully stat it again. This prevents double closes
             if the module companion already closed the fd.
  */
  struct stat st1;
  if (fstat(fd, &st1) != -1 || st0.st_ino == st1.st_ino) {
    LOGI(" - Client fd changed after module entry\n");

    close(fd);
  }

  free(args);

  return NULL;
}

/* WARNING: Dynamic memory based */
void companion_entry(int fd) {
  LOGI("New companion entry.\n - Client fd: %d\n", fd);

  char name[256 + 1];
  ssize_t ret = read_string(fd, name, sizeof(name));
  if (ret == -1) {
    LOGE("Failed to read module name\n");

    goto cleanup;
  }

  LOGI(" - Module name: \"%s\"\n", name);

  int library_fd = read_fd(fd);
  if (library_fd == -1) {
    LOGE("Failed to receive library fd\n");

    goto cleanup;
  }

  LOGI(" - Library fd: %d\n", library_fd);

  zygisk_companion_entry module_entry = load_module(library_fd);
  close(library_fd);

  if (module_entry == NULL) {
    LOGE(" - No companion module entry for module: %s\n", name);

    ret = write_uint8_t(fd, 0);
    ASSURE_SIZE_WRITE("ZygiskdCompanion", "module_entry", ret, sizeof(uint8_t));

    goto cleanup;
  } else {
    LOGI(" - Module entry found\n");

    ret = write_uint8_t(fd, 1);
    ASSURE_SIZE_WRITE("ZygiskdCompanion", "module_entry", ret, sizeof(uint8_t));
  }

  struct sigaction sa = { .sa_handler = SIG_IGN };
  sigaction(SIGPIPE, &sa, NULL);

  while (1) {
    if (!check_unix_socket(fd, true)) {
      LOGE("Something went wrong in companion. Bye!\n");

      break;
    }

    int client_fd = read_fd(fd);
    if (client_fd == -1) {
      LOGE("Failed to receive client fd\n");

      break;
    }

    struct companion_module_thread_args *args = malloc(sizeof(struct companion_module_thread_args));
    if (args == NULL) {
      LOGE("Failed to allocate memory for thread args\n");

      close(client_fd);

      break;
    }

    args->fd = client_fd;
    args->entry = module_entry;

    LOGI("New companion request.\n - Module name: %s\n - Client fd: %d\n", name, client_fd);

    ret = write_uint8_t(client_fd, 1);
    if (ret != sizeof(uint8_t)) {
      LOGE("Failed to send client_fd in ZygiskdCompanion: Expected %zu, got %zd\n", sizeof(uint8_t), ret);

      free(args);
      close(client_fd);

      break;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, entry_thread, (void *)args) == 0)
      continue;

    LOGE(" - Failed to create thread for companion module\n");

    close(client_fd);
    free(args);

    break;
  }

  cleanup:
    close(fd);
    LOGE("Companion thread exited\n");

    exit(0);
}
