#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/limits.h>
#include <pthread.h>
#include <unistd.h>

#define LOG_TAG "zygiskd-companion" LP_SELECT("32", "64")

#include "utils.h"

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
    LOGE("Failed to dlopen module: %s", dlerror());

    return NULL;
  }

  void *entry = dlsym(handle, "zygisk_companion_entry");
  if (!entry) {
    LOGE("Failed to dlsym zygisk_companion_entry: %s", dlerror());

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
    LOGE(" - Failed to get initial client fd stats: %s", strerror(errno));

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
    LOGI(" - Client fd changed after module entry");

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
    LOGE("Failed to read module name");

    goto cleanup;
  }

  LOGI(" - Module name: \"%s\"", name);

  int library_fd = read_fd(fd);
  if (library_fd == -1) {
    LOGE("Failed to receive library fd");

    goto cleanup;
  }

  LOGI(" - Library fd: %d", library_fd);

  zygisk_companion_entry module_entry = load_module(library_fd);
  close(library_fd);

  if (module_entry == NULL) {
    LOGE(" - No companion module entry for module: %s", name);

    ret = write_uint8_t(fd, 0);
    ASSURE_SIZE_WRITE("ZygiskdCompanion", "module_entry", ret, sizeof(uint8_t), goto cleanup);

    goto cleanup;
  } else {
    LOGI(" - Module entry found");

    ret = write_uint8_t(fd, 1);
    ASSURE_SIZE_WRITE("ZygiskdCompanion", "module_entry", ret, sizeof(uint8_t), goto cleanup);
  }

  struct sigaction sa = { .sa_handler = SIG_IGN };
  sigaction(SIGPIPE, &sa, NULL);

  while (1) {
    if (!check_unix_socket(fd, true)) {
      LOGE("Something went wrong in companion. Bye!");

      break;
    }

    int client_fd = read_fd(fd);
    if (client_fd == -1) {
      LOGE("Failed to receive client fd");

      break;
    }

    struct companion_module_thread_args *args = malloc(sizeof(struct companion_module_thread_args));
    if (args == NULL) {
      LOGE("Failed to allocate memory for thread args");

      close(client_fd);

      break;
    }

    args->fd = client_fd;
    args->entry = module_entry;

    LOGI("New companion request.\n - Module name: %s\n - Client fd: %d\n", name, client_fd);

    ret = write_uint8_t(client_fd, 1);
    if (ret != sizeof(uint8_t)) {
      LOGE("Failed to send client_fd in ZygiskdCompanion: Expected %zu, got %zd", sizeof(uint8_t), ret);

      free(args);
      close(client_fd);

      break;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, entry_thread, (void *)args) == 0)
      continue;

    LOGE(" - Failed to create thread for companion module");

    close(client_fd);
    free(args);

    break;
  }

  cleanup:
    close(fd);
    LOGE("Companion thread exited");

    exit(0);
}
