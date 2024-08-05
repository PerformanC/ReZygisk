#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>

#include <unistd.h>
#include <linux/limits.h>
#include <pthread.h>

#include <android/log.h>

#include "companion.h"
#include "dl.h"
#include "utils.h"

typedef void (*ZygiskCompanionEntryFn)(int);

ZygiskCompanionEntryFn load_module(int fd) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

  void *handle = android_dlopen(path, RTLD_NOW);
  void *entry = dlsym(handle, "zygisk_companion_entry");
  if (entry == NULL) return NULL;

  return (ZygiskCompanionEntryFn)entry;
}

void *ExecuteNew(void *arg) {
  int fd = *((int *)arg);

  struct stat st0;
  if (fstat(fd, &st0) == -1) {
    LOGE("Failed to stat client fd\n");

    free(arg);

    exit(0);
  }
  entry(fd);

  // Only close client if it is the same file so we don't
  // accidentally close a re-used file descriptor.
  // This check is required because the module companion
  // handler could've closed the file descriptor already.
  struct stat st1;
  if (fstat(fd, &st1) == -1) {
    LOGE("Failed to stat client fd\n");

    free(arg);

    exit(0);
  }

  if (st0.st_dev != st1.st_dev || st0.st_ino != st1.st_ino) {
    close(fd);
  }

  free(arg);

  return NULL;
}


void entry(int fd) {
  LOGI("companion entry fd: |%d|\n", fd);

  char name[256 + 1];

  /* INFO: Getting stuck here */
  ssize_t ret = read_string(fd, name, sizeof(name) - 1);
  if (ret == -1) return;

  name[ret] = '\0';

  LOGI("Companion process requested for `%s`\n", name);

  int library_fd;
  recv_fd(fd, &library_fd);

  LOGI("Library fd: %d\n", library_fd);

  ZygiskCompanionEntryFn entry = load_module(library_fd);

  LOGI("Library loaded\n");

  close(library_fd);

  LOGI("Library closed\n");

  if (entry == NULL) {
    LOGI("No companion entry for: %s\n", name);

    write(fd, (void *)0, 1);

    return;
  }

  LOGI("Companion process created for: %s\n", name);

  uint8_t response = 1;
  write(fd, &response, sizeof(response));

  while (1) {
    int client_fd;
    recv_fd(fd, &client_fd);

    LOGI("New companion request from module \"%s\" with fd \"%d\"\n", name, client_fd);

    write(fd, &response, sizeof(response));
    
    int *client_fd_ptr = malloc(sizeof(int));
    *client_fd_ptr = client_fd;

    LOGI("Creating new thread for companion request\n");

    pthread_t thread;
    pthread_create(&thread, NULL, ExecuteNew, (void *)client_fd_ptr);
    pthread_detach(thread);
  }
}
