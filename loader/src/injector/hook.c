#include <sys/mount.h>
#include <dlfcn.h>
#include <link.h>
#include <regex.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <lsplt.h>
#include <csoloader.h>

#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <signal.h>
#include <limits.h>

#include <unistd.h>
#include <pthread.h>

#include "daemon.h"
#include "module.h"
#include "misc.h"

#include "art_method.h"
#include "cpp_strings.h"

void *start_addr = NULL;
size_t block_size = 0;

/* INFO: Flag indices */
enum {
  POST_SPECIALIZE,
  APP_FORK_AND_SPECIALIZE,
  APP_SPECIALIZE,
  SERVER_FORK_AND_SPECIALIZE,
  DO_REVERT_UNMOUNT,
  SKIP_FD_SANITIZATION,

  FLAG_MAX
};

#define MAX_FD_SIZE 1024
#define MAX_REGISTER_INFO 64
#define MAX_IGNORE_INFO 64
#define MAX_EXEMPTED_FDS 128

struct register_info {
  regex_t regex;
  char *symbol;
  void *callback;
  void **backup;
};

struct ignore_info {
  regex_t regex;
  char *symbol;
};

struct zygisk_context {
  JNIEnv *env;
  union {
    void *ptr;
    struct app_specialize_args_v5 *app;
    struct server_specialize_args_v1 *server;
  } args;

  const char *process;

  int pid;
  uint32_t flags;
  uint32_t info_flags;
  uint8_t allowed_fds[MAX_FD_SIZE];
  int exempted_fds[MAX_EXEMPTED_FDS];
  size_t exempted_fds_count;

  pthread_mutex_t hook_info_lock;
  struct register_info register_info[MAX_REGISTER_INFO];
  size_t register_info_count;
  struct ignore_info ignore_info[MAX_IGNORE_INFO];
  size_t ignore_info_count;
};

/* INFO: Current context */
static struct zygisk_context *g_ctx = NULL;

/* INFO: Helper macros for flags */
#define FLAG_SET(ctx, f) ((ctx)->flags |= (1u << (f)))
#define FLAG_GET(ctx, f) (((ctx)->flags & (1u << (f))) != 0)
#define FLAG_CLEAR(ctx, f) ((ctx)->flags &= ~(1u << (f)))

#define DCL_PRE_POST(name)                                   \
  static void rz_## name ##_pre(struct zygisk_context *ctx); \
  static void rz_## name ##_post(struct zygisk_context *ctx)

/* INFO: Early declarations */
static void rz_init(struct zygisk_context *ctx, JNIEnv *env, void *args);
static void rz_cleanup(struct zygisk_context *ctx);

DCL_PRE_POST(run_modules);
DCL_PRE_POST(fork);
DCL_PRE_POST(app_specialize);
DCL_PRE_POST(nativeForkAndSpecialize);
DCL_PRE_POST(nativeSpecializeAppProcess);
DCL_PRE_POST(nativeForkSystemServer);

#undef DCL_PRE_POST

static inline bool is_zygote_child(struct zygisk_context *ctx) {
  return ctx->pid <= 0;
}

struct plt_hook_entry {
  dev_t dev;
  ino_t inode;
  const char *symbol;
  void **old_func;
};

struct jni_hook_entry {
  char *class_name;
  JNINativeMethod *methods;
  size_t methods_count;
};

static struct plt_hook_entry *plt_hook_list = NULL;
static size_t plt_hook_list_count = 0;
static size_t plt_hook_list_capacity = 0;

static struct jni_hook_entry *jni_hook_list = NULL;
static size_t jni_hook_list_count = 0;
static size_t jni_hook_list_capacity = 0;

static bool modules_loaded = false;
struct rezygisk_module *zygisk_modules = NULL;
size_t zygisk_module_length = 0;

static bool should_unmap_zygisk = false;
static bool enable_unloader = false;
static bool hooked_unloader = false;

/* INFO: Helper function to add to PLT hook list */
static void plt_hook_list_add(dev_t dev, ino_t inode, const char *symbol, void **old_func) {
  if (plt_hook_list_count >= plt_hook_list_capacity) {
    size_t new_capacity = plt_hook_list_capacity == 0 ? 16 : plt_hook_list_capacity * 2;
    struct plt_hook_entry *new_list = realloc(plt_hook_list, new_capacity * sizeof(struct plt_hook_entry));
    if (!new_list) {
      LOGE("Failed to reallocate buffer for hook list");

      return;
    }

    plt_hook_list = new_list;
    plt_hook_list_capacity = new_capacity;
  }

  plt_hook_list[plt_hook_list_count].dev = dev;
  plt_hook_list[plt_hook_list_count].inode = inode;
  plt_hook_list[plt_hook_list_count].symbol = symbol;
  plt_hook_list[plt_hook_list_count].old_func = old_func;
  plt_hook_list_count++;
}

/* INFO: Helper function to add to JNI hook list */
static void jni_hook_list_add(const char *class_name, JNINativeMethod *methods, size_t count) {
  if (jni_hook_list_count >= jni_hook_list_capacity) {
    size_t new_capacity = jni_hook_list_capacity == 0 ? 4 : jni_hook_list_capacity * 2;
    struct jni_hook_entry *new_list = realloc(jni_hook_list, new_capacity * sizeof(struct jni_hook_entry));
    if (!new_list) {
      LOGE("Failed to reallocate buffer for hook list");

      return;
    }

    jni_hook_list = new_list;
    jni_hook_list_capacity = new_capacity;
  }

  jni_hook_list[jni_hook_list_count].class_name = strdup(class_name);
  if (!jni_hook_list[jni_hook_list_count].class_name) {
    LOGE("Failed to duplicate class name for hook list");

    return;
  }

  jni_hook_list[jni_hook_list_count].methods = malloc(count * sizeof(JNINativeMethod));
  if (!jni_hook_list[jni_hook_list_count].methods) {
    LOGE("Failed to allocate memory for methods in hook list");

    free(jni_hook_list[jni_hook_list_count].class_name);

    return;
  }

  memcpy(jni_hook_list[jni_hook_list_count].methods, methods, count * sizeof(JNINativeMethod));

  jni_hook_list[jni_hook_list_count].methods_count = count;
  jni_hook_list_count++;
}

static bool update_mnt_ns(enum mount_namespace_state mns_state, bool dry_run) {
  char ns_path[PATH_MAX];
  if (!rezygiskd_update_mns(mns_state, ns_path, sizeof(ns_path))) {
    PLOGE("Failed to update mount namespace");

    return false;
  }

  if (dry_run) return true;

  int updated_ns = open(ns_path, O_RDONLY);
  if (updated_ns == -1) {
    PLOGE("Failed to open mount namespace [%s]", ns_path);

    return false;
  }

  char *mns_state_str = "unknown";
  if (mns_state == Clean) mns_state_str = "clean";
  if (mns_state == Mounted) mns_state_str = "mounted";

  LOGD("set mount namespace to [%s] fd=[%d]: %s", ns_path, updated_ns, mns_state_str);

  if (setns(updated_ns, CLONE_NEWNS) == -1) {
    PLOGE("Failed to set mount namespace [%s]", ns_path);

    close(updated_ns);

    return false;
  }

  close(updated_ns);

  return true;
}

/* INFO: Hook function declarations */
#define DCL_HOOK_FUNC(ret, func, ...) \
  ret (*old_##func)(__VA_ARGS__);     \
  ret new_##func(__VA_ARGS__)

/* INFO: ReZygisk already performs a fork in zygisk_context::fork_pre, because of that,
           we avoid duplicate fork in nativeForkAndSpecialize and nativeForkSystemServer
           by caching the pid in fork_pre function and only performing fork if the pid
           is non-0, or in other words, if we (libzygisk.so) already forked.
*/
DCL_HOOK_FUNC(int, fork) {
  return (g_ctx && g_ctx->pid >= 0) ? g_ctx->pid : old_fork();
}

/* INFO: file_path is a std::string in the actual class. We represent it as opaque bytes. */
#ifdef __LP64__
  #define STD_STRING_SIZE 24
#else
  #define STD_STRING_SIZE 12
#endif

struct FileDescriptorInfo {
  const int fd;
  const struct stat stat;
  /* INFO: std::string, actually */
  char file_path_storage[STD_STRING_SIZE];
  const int open_flags;
  const int fd_flags;
  const int fs_flags;
  const off_t offset;
  const bool is_sock;
};

/* INFO: This hook avoids that umounted overlays made by root modules lead to Zygote
           to Abort its operation as it cannot open anymore.

   SOURCES:
     - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-14.0.0_r1/core/jni/fd_utils.cpp#346
     - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-14.0.0_r1/core/jni/fd_utils.cpp#544
     - https://android.googlesource.com/platform/frameworks/base/+/refs/tags/android-14.0.0_r1/core/jni/com_android_internal_os_Zygote.cpp#2329
*/
DCL_HOOK_FUNC(void, _ZNK18FileDescriptorInfo14ReopenOrDetach, void *_this, void *fail_fn) {
  const int fd = *(const int *)((uintptr_t)_this + offsetof(struct FileDescriptorInfo, fd));
  const void *file_path_std_string = (const void *)((uintptr_t)_this + offsetof(struct FileDescriptorInfo, file_path_storage));
  const char *file_path = read_std_string(file_path_std_string);
  const bool is_sock = *(const bool *)((uintptr_t)_this + offsetof(struct FileDescriptorInfo, is_sock));

  if (is_sock)
    goto bypass_fd_check;

  if (strncmp(file_path, "/memfd:/boot-image-methods.art", strlen("/memfd:/boot-image-methods.art")) == 0)
    goto bypass_fd_check;

  if (access(file_path, F_OK) == -1) {
    LOGD("Failed to open file %s, detaching it", file_path);

    close(fd);

    return;
  }

  bypass_fd_check:
    old__ZNK18FileDescriptorInfo14ReopenOrDetach(_this, fail_fn);
}

static void unhook_functions(void);
/* INFO: Self-unloading is not a direct task, it requires the utilization of tail
           optimization, which requires the signature to be the same as munmap, or
           else munmap will be executed and will try to reach our code, leading to
           a segmentation fault.

         To counter that, we hook pthread_attr_setstacksize, which is called around
           when the VM daemon starts, to allow this to happen before the app can
           execute code.
*/
DCL_HOOK_FUNC(int, pthread_attr_setstacksize, void *target, size_t size) {
  int res = old_pthread_attr_setstacksize((pthread_attr_t *)target, size);
  LOGV("Call pthread_attr_setstacksize in [tid, pid]: %d, %d", gettid(), getpid());

  if (!enable_unloader) return res;

  /* INFO: Only perform unloading on the main thread */
  if (gettid() != getpid()) return res;

  if (should_unmap_zygisk) {
    unhook_functions();

    /* INFO: Modules might use libzygisk.so after postAppSpecialize. We can only
               free it when we are really before our unmap. */
    free(zygisk_modules);
    zygisk_modules = NULL;

    lsplt_free_resources();

    LOGD("unmap libzygisk.so loaded at %p with size %zu", start_addr, block_size);

    [[clang::musttail]] return munmap(start_addr, block_size);
  }

  return res;
}

static void initialize_jni_hook(void);
DCL_HOOK_FUNC(char *, strdup, const char *str) {
  if (strcmp(str, "com.android.internal.os.ZygoteInit") == 0) {
    LOGV("strdup %s", str);

    initialize_jni_hook();
  }

  return old_strdup(str);
}

static void hook_unloader(void);
/*
  INFO: Our goal is to get called after libart.so is loaded, but before ART actually starts running.
          If we are too early, we won't find libart.so in maps, and if we are too late, we could make other
          threads crash if they try to use the PLT while we are in the process of hooking it.
          For this task, hooking property_get was chosen as there are lots of calls to this, so it's
          relatively unlikely to break.

        After we succeed in getting called at a point where libart.so is already loaded, we will ignore
          the rest of the property_get calls.
  SOURCES:
   - https://github.com/aosp-mirror/platform_frameworks_base/blob/1cdfff555f4a21f71ccc978290e2e212e2f8b168/core/jni/AndroidRuntime.cpp#L1266
   - https://github.com/aosp-mirror/platform_frameworks_base/blob/1cdfff555f4a21f71ccc978290e2e212e2f8b168/core/jni/AndroidRuntime.cpp#L791
*/
DCL_HOOK_FUNC(int, property_get, const char *key, char *value, const char *default_value) {
  hook_unloader();

  return old_property_get(key, value, default_value);
}

#undef DCL_HOOK_FUNC

static bool can_hook_jni = false;
static jint MODIFIER_NATIVE = 0;
static jmethodID member_getModifiers = NULL;

void hook_jni_methods(JNIEnv *env, const char *clz, JNINativeMethod *methods, int numMethods) {
  if (!can_hook_jni) return;

  jclass clazz = (*env)->FindClass(env, clz);
  if (!clazz) {
    (*env)->ExceptionClear(env);

    memset(methods, 0, numMethods * sizeof(JNINativeMethod));

    return;
  }

  JNINativeMethod hooks[32];
  size_t hooks_count = 0;

  for (int i = 0; i < numMethods; i++) {
    bool is_static = false;

    JNINativeMethod *nm = &methods[i];
    jmethodID mid = (*env)->GetMethodID(env, clazz, nm->name, nm->signature);
    if (!mid) {
      (*env)->ExceptionClear(env);
      mid = (*env)->GetStaticMethodID(env, clazz, nm->name, nm->signature);
      is_static = true;
    }

    if (!mid) {
      (*env)->ExceptionClear(env);
      nm->fnPtr = NULL;

      continue;
    }

    jobject method = (*env)->ToReflectedMethod(env, clazz, mid, is_static);
    jint modifier = (*env)->CallIntMethod(env, method, member_getModifiers);
    if ((modifier & MODIFIER_NATIVE) == 0) {
      nm->fnPtr = NULL;

      continue;
    }

    void *art_method = amethod_from_reflected_method(env, method);
    if (hooks_count < 32)
      hooks[hooks_count++] = *nm;

    void *orig = amethod_get_data((uintptr_t)art_method);
    nm->fnPtr = orig;

    LOGV("replaced %s %s orig %p", clz, nm->name, orig);
  }

  if (hooks_count == 0) {
    (*env)->DeleteLocalRef(env, clazz);

    return;
  }

  (*env)->RegisterNatives(env, clazz, hooks, (jint)hooks_count);
  (*env)->DeleteLocalRef(env, clazz);
}

/* INFO: JNI method hook definitions */
#include "jni_hooks.h"

static void initialize_jni_hook(void) {
  jint (*get_created_java_vms)(JavaVM **, jsize, jsize *) = (jint (*)(JavaVM **, jsize, jsize *))dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
  if (!get_created_java_vms) {
    struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
    if (!map_infos) {
      LOGE("Failed to scan maps for self");

      return;
    }

    for (size_t i = 0; i < map_infos->length; i++) {
      struct lsplt_map_entry *map = &map_infos->maps[i];
      if (!strstr(map->path, "/libnativehelper.so")) continue;

      /* Pre-O linker lacks namespaces; load libnativehelper globally to export JNI_GetCreatedJavaVMs. */
      void *handle = dlopen(map->path, RTLD_NOW | RTLD_GLOBAL);
      if (!handle) {
        LOGE("Failed to dlopen %s: %s", map->path, dlerror());

        break;
      }
 
      get_created_java_vms = (jint (*)(JavaVM **, jsize, jsize *))dlsym(handle, "JNI_GetCreatedJavaVMs");
      dlclose(handle);

      break;
    }

    lsplt_free_maps(map_infos);

    if (!get_created_java_vms) {
      LOGE("Failed to find JNI_GetCreatedJavaVMs");

      return;
    }
  }

  JavaVM *vm = NULL;
  jsize num = 0;
  jint res = get_created_java_vms(&vm, 1, &num);
  if (res != JNI_OK || !vm) return;

  JNIEnv *env = NULL;
  res = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6);
  if (res != JNI_OK || !env) return;

  jclass classMember = (*env)->FindClass(env, "java/lang/reflect/Member");
  if (classMember) member_getModifiers = (*env)->GetMethodID(env, classMember, "getModifiers", "()I");

  jclass classModifier = (*env)->FindClass(env, "java/lang/reflect/Modifier");
  if (classModifier) {
    jfieldID fieldId = (*env)->GetStaticFieldID(env, classModifier, "NATIVE", "I");
    if (fieldId) MODIFIER_NATIVE = (*env)->GetStaticIntField(env, classModifier, fieldId);
  }

  (*env)->DeleteLocalRef(env, classMember);
  (*env)->DeleteLocalRef(env, classModifier);

  if (!member_getModifiers || MODIFIER_NATIVE == 0) return;

  if (!amethod_init(env)) {
    LOGE("failed to init amethod");

    return;
  }

  can_hook_jni = true;
  do_hook_zygote(env);
}

/* INFO: Module registration and API functions */
static void api_plt_hook_register(const char *regex, const char *symbol, void *fn, void **backup) {
  if (!g_ctx || !regex || !symbol || !fn || g_ctx->register_info_count >= MAX_REGISTER_INFO) return;

  regex_t re;
  if (regcomp(&re, regex, REG_NOSUB) != 0) return;

  pthread_mutex_lock(&g_ctx->hook_info_lock);

  g_ctx->register_info[g_ctx->register_info_count].regex = re;
  g_ctx->register_info[g_ctx->register_info_count].symbol = strdup(symbol);
  g_ctx->register_info[g_ctx->register_info_count].callback = fn;
  g_ctx->register_info[g_ctx->register_info_count].backup = backup;
  g_ctx->register_info_count++;

  pthread_mutex_unlock(&g_ctx->hook_info_lock);
}

static void api_plt_hook_exclude(const char *regex, const char *symbol) {
  if (!g_ctx || !regex || g_ctx->ignore_info_count >= MAX_IGNORE_INFO) return;

  regex_t re;
  if (regcomp(&re, regex, REG_NOSUB) != 0) return;

  pthread_mutex_lock(&g_ctx->hook_info_lock);

  g_ctx->ignore_info[g_ctx->ignore_info_count].regex = re;
  g_ctx->ignore_info[g_ctx->ignore_info_count].symbol = symbol ? strdup(symbol) : NULL;
  g_ctx->ignore_info_count++;

  pthread_mutex_unlock(&g_ctx->hook_info_lock);
}

static bool api_plt_hook_commit(void) {
  if (!g_ctx || g_ctx->register_info_count == 0) return false;

  pthread_mutex_lock(&g_ctx->hook_info_lock);

  struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
  if (!map_infos) {
    LOGE("Failed to scan maps for self");

    pthread_mutex_unlock(&g_ctx->hook_info_lock);

    return false;
  }

  for (size_t i = 0; i < map_infos->length; i++) {
    struct lsplt_map_entry *map = &map_infos->maps[i];
    if (map->offset != 0 || !map->is_private || !(map->perms & PROT_READ)) continue;

    for (size_t r = 0; r < g_ctx->register_info_count; r++) {
      struct register_info *reg = &g_ctx->register_info[r];
      if (regexec(&reg->regex, map->path, 0, NULL, 0) != 0) continue;

      bool ignored = false;
      for (size_t ig = 0; ig < g_ctx->ignore_info_count; ig++) {
        struct ignore_info *ign = &g_ctx->ignore_info[ig];
        if (regexec(&ign->regex, map->path, 0, NULL, 0) != 0) continue;  
        if (ign->symbol && strcmp(ign->symbol, reg->symbol) != 0) continue;

        ignored = true;

        break;
      }

      if (!ignored)
        lsplt_register_hook(map->dev, map->inode, reg->symbol, reg->callback, reg->backup);
    }
  }

  lsplt_free_maps(map_infos);

  /* INFO: Clear register_info and ignore_info */
  for (size_t i = 0; i < g_ctx->register_info_count; i++) {
    regfree(&g_ctx->register_info[i].regex);
    free(g_ctx->register_info[i].symbol);
  }
  g_ctx->register_info_count = 0;

  for (size_t i = 0; i < g_ctx->ignore_info_count; i++) {
    regfree(&g_ctx->ignore_info[i].regex);
    free(g_ctx->ignore_info[i].symbol);
  }
  g_ctx->ignore_info_count = 0;

  pthread_mutex_unlock(&g_ctx->hook_info_lock);

  return lsplt_commit_hook();
}

static void api_plt_hook_register_v4(dev_t dev, ino_t inode, const char *symbol, void *fn, void **backup) {
  if (!g_ctx) return;

  if (dev == 0 || inode == 0 || !symbol || !fn) {
    LOGE("Invalid arguments to plt_hook_register");

    return;
  }

  lsplt_register_hook(dev, inode, symbol, fn, backup);
}

static void api_exempt_fd(int fd) {
  if (!g_ctx) return;

  if (FLAG_GET(g_ctx, POST_SPECIALIZE) || FLAG_GET(g_ctx, SKIP_FD_SANITIZATION)) return;
  if (!FLAG_GET(g_ctx, APP_FORK_AND_SPECIALIZE)) return;
  if (g_ctx->exempted_fds_count >= MAX_EXEMPTED_FDS) return;

  g_ctx->exempted_fds[g_ctx->exempted_fds_count++] = fd;

  return;
}

static bool api_plt_hook_commit_v4(void) {
  if (!g_ctx) return false;

  return lsplt_commit_hook();
}

static int api_connect_companion(void *id) {
  if (!g_ctx) return -1;

  if ((size_t)id >= zygisk_module_length) {
    LOGE("Invalid module id %zu", (size_t)id);

    return -1;
  }

  return rezygiskd_connect_companion((size_t)id);
}

static void api_set_option(void *id, enum rezygisk_options opt) {
  if (!g_ctx) return;

  if ((size_t)id >= zygisk_module_length) {
    LOGE("Invalid module id %zu", (size_t)id);

    return;
  }

  switch (opt) {
    case FORCE_DENYLIST_UNMOUNT: {
      FLAG_SET(g_ctx, DO_REVERT_UNMOUNT);

      break;
    }
    case DLCLOSE_MODULE_LIBRARY: {
      struct rezygisk_module *m_lib = &zygisk_modules[(size_t)id];
      m_lib->unload = true;

      break;
    }
  }
}

static int api_get_module_dir(void *id) {
  if (!g_ctx) return -1;

  if ((size_t)id >= zygisk_module_length) {
    LOGE("Invalid module id %zu", (size_t)id);

    return -1;
  }

  return rezygiskd_get_module_dir((size_t)id);
}

static uint32_t api_get_flags(void) {
  if (!g_ctx) return 0;

  return (g_ctx->info_flags & ~PRIVATE_MASK);
}

bool rezygisk_module_register(struct rezygisk_api *api, struct rezygisk_abi const *target_module) {
  if (!g_ctx || !api || !target_module || target_module->api_version > REZYGISK_API_VERSION) return false;

  LOGD("Registering module with API version %ld", target_module->api_version);

  struct rezygisk_module *m = &zygisk_modules[(size_t)api->impl];
  m->abi = *target_module;
  m->api = *api;

  api->hook_jni_native_methods = hook_jni_methods;
  if (target_module->api_version >= 4) {
    api->plt_hook_register_v4 = api_plt_hook_register_v4;
    api->exempt_fd = api_exempt_fd;
    api->plt_hook_commit = api_plt_hook_commit_v4;
  } else {
    api->plt_hook_register = api_plt_hook_register;
    api->plt_hook_exclude = api_plt_hook_exclude;
    api->plt_hook_commit = api_plt_hook_commit;
  }

  api->connect_companion = api_connect_companion;
  api->set_option = api_set_option;

  if (target_module->api_version >= 2) {
    api->get_module_dir = api_get_module_dir;
    api->get_flags = api_get_flags;
  }

  return true;
}

/* INFO: Signal mask helper */
static int sigmask(int how, int signum) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, signum);

  return sigprocmask(how, &set, NULL);
}

static bool load_modules_only(void);

static void rz_fork_pre(struct zygisk_context *ctx) {
  /* INFO: Do our own fork before loading any 3rd party code.
              First block SIGCHLD, unblock after original fork is done.
  */
  sigmask(SIG_BLOCK, SIGCHLD);
  ctx->pid = old_fork();
  if (ctx->pid != 0 || FLAG_GET(ctx, SKIP_FD_SANITIZATION)) return;

  /* INFO: Record all open fds */
  DIR *dir = opendir("/proc/self/fd");
  if (!dir) {
    PLOGE("Failed to open /proc/self/fd");

    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir))) {
    int fd = parse_int(entry->d_name);
    if (fd < 0 || fd >= MAX_FD_SIZE) {
      close(fd);

      continue;
    }

    ctx->allowed_fds[fd] = 1;
  }

  /* INFO: The dirfd should not be allowed */
  int dfd = dirfd(dir);
  if (dfd >= 0 && dfd < MAX_FD_SIZE) ctx->allowed_fds[dfd] = 0;

  closedir(dir);
}

static void mark_fds_allowed(struct zygisk_context *ctx, JNIEnv *env, jintArray fdsArray) {
  if (!fdsArray) return;

  jint *arr = (*env)->GetIntArrayElements(env, fdsArray, NULL);
  jint len = (*env)->GetArrayLength(env, fdsArray);

  for (jint i = 0; i < len; ++i) {
    int fd = arr[i];
    if (fd >= 0 && fd < MAX_FD_SIZE) ctx->allowed_fds[fd] = 1;
  }

  (*env)->ReleaseIntArrayElements(env, fdsArray, arr, JNI_ABORT);
}

static void rz_sanitize_fds(struct zygisk_context *ctx) {
  if (FLAG_GET(ctx, SKIP_FD_SANITIZATION)) return;

  if (FLAG_GET(ctx, APP_FORK_AND_SPECIALIZE)) {
    jintArray fdsToIgnore = *ctx->args.app->fds_to_ignore;
    mark_fds_allowed(ctx, ctx->env, fdsToIgnore);

    if (ctx->exempted_fds_count > 0) {
      jint len = fdsToIgnore ? (*ctx->env)->GetArrayLength(ctx->env, fdsToIgnore) : 0;
      jintArray newArray = (*ctx->env)->NewIntArray(ctx->env, (jsize)(len + ctx->exempted_fds_count));
      if (newArray) {
        if (fdsToIgnore && len > 0) {
          jint *arr = (*ctx->env)->GetIntArrayElements(ctx->env, fdsToIgnore, NULL);
          (*ctx->env)->SetIntArrayRegion(ctx->env, newArray, 0, len, arr);
          (*ctx->env)->ReleaseIntArrayElements(ctx->env, fdsToIgnore, arr, JNI_ABORT);
          (*ctx->env)->DeleteLocalRef(ctx->env, fdsToIgnore);
        }

        (*ctx->env)->SetIntArrayRegion(ctx->env, newArray, len, (jsize)ctx->exempted_fds_count, ctx->exempted_fds);
        for (size_t i = 0; i < ctx->exempted_fds_count; i++) {
          int fd = ctx->exempted_fds[i];
          if (fd >= 0 && fd < MAX_FD_SIZE) ctx->allowed_fds[fd] = 1;
        }

        *ctx->args.app->fds_to_ignore = newArray;
        FLAG_SET(ctx, SKIP_FD_SANITIZATION);
      }
    }
  }

  if (ctx->pid != 0) return;

  /* INFO: Close all forbidden fds to prevent crashing */
  DIR *dir = opendir("/proc/self/fd");
  if (!dir) {
    PLOGE("Failed to open /proc/self/fd");

    return;
  }

  int dfd = dirfd(dir);
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    int fd = parse_int(entry->d_name);
    if (fd < 0 || fd >= MAX_FD_SIZE || fd == dfd || ctx->allowed_fds[fd]) continue;

    close(fd);

    LOGW("Closed leaked fd: %d", fd);
  }

  closedir(dir);
}

static void rz_fork_post(struct zygisk_context *ctx __attribute__((unused))) {
  sigmask(SIG_UNBLOCK, SIGCHLD);
  g_ctx = NULL;
}

static bool load_modules_only(void) {
  struct zygisk_modules ms;
  if (!rezygiskd_read_modules(&ms)) {
    LOGE("Failed to read modules from ReZygiskd");

    return false;
  }

  zygisk_modules = (struct rezygisk_module *)malloc(ms.modules_count * sizeof(struct rezygisk_module));
  if (!zygisk_modules) {
    LOGE("Failed to allocate memory for modules");

    free_modules(&ms);

    return false;
  }

  for (size_t i = 0; i < ms.modules_count; i++) {
    const char *lib_path = ms.modules[i];

    if (!csoloader_load(&zygisk_modules[zygisk_module_length].lib, lib_path)) {
      LOGE("Failed to load module [%s]", lib_path);

      continue;
    }

    void *entry = csoloader_get_symbol(&zygisk_modules[zygisk_module_length].lib, "zygisk_module_entry");
    if (!entry) {
      LOGE("Failed to find entry point in module [%s]", lib_path);

      csoloader_unload(&zygisk_modules[zygisk_module_length].lib);

      continue;
    }

    zygisk_modules[zygisk_module_length].api.register_module = rezygisk_module_register;
    zygisk_modules[zygisk_module_length].api.impl = (void *)zygisk_module_length;
    zygisk_modules[zygisk_module_length].zygisk_module_entry = (void (*)(void *, void *))entry;

    LOGD("Loaded module [%s]. Entry: %p", lib_path, entry);

    zygisk_modules[zygisk_module_length].unload = false;
    zygisk_module_length++;
  }

  free_modules(&ms);

  return true;
}

static void rz_run_modules_pre(struct zygisk_context *ctx) {
  for (size_t i = 0; i < zygisk_module_length; i++) {
    rz_module_call_on_load(&zygisk_modules[i], ctx->env);

    if (FLAG_GET(ctx, APP_SPECIALIZE)) rz_module_call_pre_app_specialize(&zygisk_modules[i], ctx->args.app);
    else if (FLAG_GET(ctx, SERVER_FORK_AND_SPECIALIZE)) rz_module_call_pre_server_specialize(&zygisk_modules[i], ctx->args.server);
  }
}

static void rz_run_modules_post(struct zygisk_context *ctx) {
  FLAG_SET(ctx, POST_SPECIALIZE);

  size_t modules_unloaded = 0;
  for (size_t i = 0; i < zygisk_module_length; i++) {
    struct rezygisk_module *m = &zygisk_modules[i];

    if (FLAG_GET(ctx, APP_SPECIALIZE)) rz_module_call_post_app_specialize(m, ctx->args.app);
    else if (FLAG_GET(ctx, SERVER_FORK_AND_SPECIALIZE)) rz_module_call_post_server_specialize(m, ctx->args.server);

    if (!m->unload) {
      LOGD("Abandoning module library at %p", &m->lib);

      csoloader_abandon(&m->lib);

      continue;
    }

    if (!csoloader_unload(&m->lib)) {
      LOGE("Failed to unload module library");

      continue;
    }

    modules_unloaded++;
  }

  if (zygisk_module_length > 0)
    LOGD("Modules unloaded: %zu/%zu", modules_unloaded, zygisk_module_length);
}

static void rz_app_specialize_pre(struct zygisk_context *ctx) {
  FLAG_SET(ctx, APP_SPECIALIZE);


  /* INFO: Isolated services have different UIDs than the main apps. Because
              numerous root implementations base themselves in the UID of the
              app, we need to ensure that the UID sent to ReZygiskd to search
              is the app's and not the isolated service, or else it will be
              able to bypass DenyList.

           All apps, and isolated processes, of *third-party* applications will
             have their app_data_dir set. The system applications might not have
             one, however it is unlikely they will create an isolated process,
             and even if so, it should not impact in detections, performance or
             any area.
  */
  uid_t uid = *ctx->args.app->uid;
  if (IS_ISOLATED_SERVICE(uid) && ctx->args.app->app_data_dir) {
    /* INFO: If the app is an isolated service, we use the UID of the
               app's process data directory, which is the UID of the
               app itself, which root implementations actually use.
    */
    const char *data_dir = (*ctx->env)->GetStringUTFChars(ctx->env, *ctx->args.app->app_data_dir, NULL);
    if (!data_dir) {
      LOGE("Failed to get app data directory");

      return;
    }

    struct stat st;
    if (stat(data_dir, &st) == -1) {
      PLOGE("Failed to stat app data directory [%s]", data_dir);

      (*ctx->env)->ReleaseStringUTFChars(ctx->env, *ctx->args.app->app_data_dir, data_dir);

      return;
    }

    uid = st.st_uid;
    LOGD("Isolated service being related to UID %d, app data dir: %s", uid, data_dir);

    (*ctx->env)->ReleaseStringUTFChars(ctx->env, *ctx->args.app->app_data_dir, data_dir);
  }

  ctx->info_flags = rezygiskd_get_process_flags(uid, ctx->process);
  if (ctx->info_flags & PROCESS_IS_FIRST_STARTED) {
    /* INFO: To ensure we are really using a clean mount namespace, we use
               the first process it as reference for clean mount namespace,
               before it even does something, so that it will be clean yet
               with expected mounts.
    */
    update_mnt_ns(Clean, true);
  }

  if ((ctx->info_flags & PROCESS_IS_MANAGER) == PROCESS_IS_MANAGER) {
    LOGD("Manager process detected. Notifying that Zygisk has been enabled.");


    /* INFO: This environment variable is related to Magisk Zygisk/Manager. It
               it used by Magisk's Zygisk to communicate to Magisk Manager whether
               Zygisk is working or not, allowing Zygisk modules to both work properly
               and for the manager to mark Zygisk as enabled.

             However, to enhance capabilities of root managers, it is also set for
               any other supported manager, so that, if they wish, they can recognize
               if Zygisk is enabled.
    */
    setenv("ZYGISK_ENABLED", "1", 1);
  }

  /* INFO: Modules only have two "start off" points from Zygisk, preSpecialize and
             postSpecialize. In preSpecialize, the process still has privileged
             permissions, and therefore can execute mount/umount/setns functions.
             If we update the mount namespace AFTER executing them, any mounts made
             will be lost, and the process will not have access to them anymore.

           In postSpecialize, while still could have its mounts modified with the
             assistance of a Zygisk companion, it will already have the mount
             namespace switched by then, so there won't be issues.

           Knowing this, we update the mns before execution, so that they can still
             make changes to mounts in DenyListed processes without being reverted.
  */
  bool in_denylist = (ctx->info_flags & PROCESS_ON_DENYLIST) == PROCESS_ON_DENYLIST;
  if (in_denylist) {
    FLAG_SET(ctx, DO_REVERT_UNMOUNT);
    update_mnt_ns(Clean, false);
  }

  /* INFO: Executed after setns to ensure a module can update the mounts of an
              application without worrying about it being overwritten by setns.
  */
  rz_run_modules_pre(ctx);

  /* INFO: The modules may request that although the process is NOT in
              the DenyList, it has its mount namespace switched to the clean
              one.

              So to ensure this behavior happens, we must also check after the
              modules are loaded and executed, so that the modules can have
              the chance to request it.
  */
  if (!in_denylist && FLAG_GET(ctx, DO_REVERT_UNMOUNT))
    update_mnt_ns(Clean, false);
}

static void rz_app_specialize_post(struct zygisk_context *ctx) {
  rz_run_modules_post(ctx);

  /* INFO: Allow the process name string to be released */
  (*ctx->env)->ReleaseStringUTFChars(ctx->env, *ctx->args.app->nice_name, ctx->process);
  g_ctx = NULL;
}

static void rz_nativeSpecializeAppProcess_pre(struct zygisk_context *ctx) {
  ctx->process = (*ctx->env)->GetStringUTFChars(ctx->env, *ctx->args.app->nice_name, NULL);
  LOGV("pre specialize [%s]", ctx->process);

  FLAG_SET(ctx, SKIP_FD_SANITIZATION);
  rz_app_specialize_pre(ctx);
}

static void rz_nativeSpecializeAppProcess_post(struct zygisk_context *ctx) {
  LOGV("post specialize [%s]", ctx->process);
  rz_app_specialize_post(ctx);
}

static void rz_nativeForkSystemServer_pre(struct zygisk_context *ctx) {
  LOGV("pre forkSystemServer");
  FLAG_SET(ctx, SERVER_FORK_AND_SPECIALIZE);
  /* Avoid fd sanitization for system_server to keep critical descriptors intact. */
  FLAG_SET(ctx, SKIP_FD_SANITIZATION);

  rz_fork_pre(ctx);
  if (!is_zygote_child(ctx)) return;

  rz_run_modules_pre(ctx);
  rezygiskd_system_server_started();
}

static void rz_nativeForkSystemServer_post(struct zygisk_context *ctx) {
  if (ctx->pid == 0) {
    LOGV("post forkSystemServer");

    rz_run_modules_post(ctx);
  }

  rz_fork_post(ctx);
}

static void rz_nativeForkAndSpecialize_pre(struct zygisk_context *ctx) {
  ctx->process = (*ctx->env)->GetStringUTFChars(ctx->env, *ctx->args.app->nice_name, NULL);
  LOGV("pre forkAndSpecialize [%s]", ctx->process);
  FLAG_SET(ctx, APP_FORK_AND_SPECIALIZE);

  rz_fork_pre(ctx);
  if (!is_zygote_child(ctx)) return;

  rz_app_specialize_pre(ctx);
  rz_sanitize_fds(ctx);
}

static void rz_nativeForkAndSpecialize_post(struct zygisk_context *ctx) {
  if (ctx->pid == 0) {
    LOGV("post forkAndSpecialize [%s]", ctx->process);
    rz_app_specialize_post(ctx);
  }

  rz_fork_post(ctx);
}

static void rz_init(struct zygisk_context *ctx, JNIEnv *env, void *args) {
  memset(ctx, 0, sizeof(struct zygisk_context));

  ctx->env = env;
  ctx->args.ptr = args;
  ctx->pid = -1;
  pthread_mutex_init(&ctx->hook_info_lock, NULL);

  g_ctx = ctx;
}

static void rz_cleanup(struct zygisk_context *ctx) {
  g_ctx = NULL;

  if (!is_zygote_child(ctx)) return;

  should_unmap_zygisk = true;

  /* INFO: Unhook JNI methods */
  for (size_t i = 0; i < jni_hook_list_count; i++) {
    struct jni_hook_entry *entry = &jni_hook_list[i];
    jclass jc = (*ctx->env)->FindClass(ctx->env, entry->class_name);
    if (jc) {
      if (entry->methods_count > 0 && (*ctx->env)->RegisterNatives(ctx->env, jc, entry->methods, (jint)entry->methods_count) != 0) {
        LOGE("Failed to restore JNI hook of class [%s]", entry->class_name);

        should_unmap_zygisk = false;
      }

      (*ctx->env)->DeleteLocalRef(ctx->env, jc);
    }

    free(entry->class_name);
    free(entry->methods);
  }

  free(jni_hook_list);
  jni_hook_list = NULL;
  jni_hook_list_count = 0;
  jni_hook_list_capacity = 0;

  /* INFO: Strip out all API function pointers */
  for (size_t i = 0; i < zygisk_module_length; i++) {
    memset(&zygisk_modules[i], 0, sizeof(zygisk_modules[i]));
  }

  enable_unloader = true;
  pthread_mutex_destroy(&ctx->hook_info_lock);
}

/* INFO: PLT hook commit helper */
static bool hook_commit(struct lsplt_map_info *map_infos) {
  if (map_infos ? lsplt_commit_hook_manual(map_infos) : lsplt_commit_hook()) return true;
  else {
    LOGE("Failed to commit hook for %zu symbols", plt_hook_list_count);

    return false;
  }
}

static void hook_register(dev_t dev, ino_t inode, const char *symbol, bool is_prefix, void *new_func, void **old_func) {
  bool res = false;
  if (is_prefix) res = lsplt_register_hook_by_prefix(dev, inode, symbol, new_func, old_func);
  else res = lsplt_register_hook(dev, inode, symbol, new_func, old_func);

  if (!res) {
    LOGE("Failed to register plt_hook \"%s\"", symbol);

    return;
  }

  plt_hook_list_add(dev, inode, symbol, old_func);
}

#define PLT_HOOK_REGISTER_SYM(DEV, INODE, SYM, NAME, IS_PREFIX)                      \
  hook_register(DEV, INODE, SYM, IS_PREFIX, (void*)new_##NAME, (void **)&old_##NAME)

#define PLT_HOOK_REGISTER(DEV, INODE, NAME, IS_PREFIX)      \
  PLT_HOOK_REGISTER_SYM(DEV, INODE, #NAME, NAME, IS_PREFIX)

void hook_functions(void) {
  ino_t android_runtime_inode = 0;
  dev_t android_runtime_dev = 0;

  struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
  if (!map_infos) {
    LOGE("Failed to scan maps for self");

    return;
  }

  for (size_t i = 0; i < map_infos->length; i++) {
    struct lsplt_map_entry map = map_infos->maps[i];

    if (!strstr(map.path, "libandroid_runtime.so")) continue;

    android_runtime_inode = map.inode;
    android_runtime_dev = map.dev;

    LOGD("Found libandroid_runtime.so at [%zu:%lu]", android_runtime_dev, android_runtime_inode);

    break;
  }

  PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, fork, false);
  PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, strdup, false);
  PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, property_get, false);
  PLT_HOOK_REGISTER_SYM(android_runtime_dev, android_runtime_inode, "_ZNK18FileDescriptorInfo14ReopenOrDetach", _ZNK18FileDescriptorInfo14ReopenOrDetach, true);

  if (!hook_commit(map_infos)) {
    plt_hook_list_count = 0;

    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, fork, false);
    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, strdup, false);
    PLT_HOOK_REGISTER(android_runtime_dev, android_runtime_inode, property_get, false);

    if (hook_commit(map_infos)) {
      LOGW("Hooked without ReopenOrDetach hook! Umounting overlays will cause problems");
    } else {
      LOGE("Failed to hook neccessary symbols to function");
    }
  }

  lsplt_free_maps(map_infos);
}

static void hook_unloader(void) {
  if (hooked_unloader) return;
  hooked_unloader = true;

  ino_t art_inode = 0;
  dev_t art_dev = 0;

  struct lsplt_map_info *map_infos = lsplt_scan_maps("self");
  if (!map_infos) {
    LOGE("Failed to scan maps for self");

    return;
  }

  for (size_t i = 0; i < map_infos->length; i++) {
    struct lsplt_map_entry *map = &map_infos->maps[i];
    if (!strstr(map->path, "/libart.so")) continue;

    art_inode = map->inode;
    art_dev = map->dev;

    LOGD("Found libart.so at [%zu:%lu]", art_dev, art_inode);

    break;
  }

  if (art_dev == 0 || art_inode == 0) {
    LOGE("Could not find libart.so. Unloader hook failed");

    hooked_unloader = false;
  } else {
    LOGD("Successfully found libart.so [%zu:%lu] for ReZygisk unloading", art_dev, art_inode);

    PLT_HOOK_REGISTER(art_dev, art_inode, pthread_attr_setstacksize, false);
    hook_commit(map_infos);
  }

  lsplt_free_maps(map_infos);

    /* INFO: Load modules early on (before system server fork) to spread through all Zygotes */
  if (!modules_loaded) {
    if (!load_modules_only()) {
      LOGE("Failed to load modules in hook_unloader");
    } else modules_loaded = true;
  }
}

static void unhook_functions(void) {
  /* INFO: Unhook plt_hook */
  for (size_t i = 0; i < plt_hook_list_count; i++) {
    struct plt_hook_entry *entry = &plt_hook_list[i];
    if (!lsplt_register_hook(entry->dev, entry->inode, entry->symbol, *entry->old_func, NULL)) {
      LOGE("Failed to register plt_hook [%s]", entry->symbol);
    }
  }

  free(plt_hook_list);
  plt_hook_list = NULL;
  plt_hook_list_count = 0;
  plt_hook_list_capacity = 0;

  if (!hook_commit(NULL)) {
    LOGE("Failed to restore plt_hook");

    should_unmap_zygisk = false;
  }
}
