#include <dlfcn.h>
#include <android/log.h>

#define LOG_TAG "ZyStab"
#define LOGD(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)

/* Minimal Zygisk API surface */
struct zygisk_ctx;
typedef void (*zygisk_on_load_fn)(struct zygisk_ctx *ctx);
typedef void (*zygisk_pre_app_specialize_fn)(struct zygisk_ctx *ctx);
typedef void (*zygisk_post_app_specialize_fn)(struct zygisk_ctx *ctx);
typedef void (*zygisk_pre_server_specialize_fn)(struct zygisk_ctx *ctx);
typedef void (*zygisk_post_server_specialize_fn)(struct zygisk_ctx *ctx);

struct zygisk_module_v3 {
    int version;
    const char *name;
    zygisk_on_load_fn on_load;
    zygisk_pre_app_specialize_fn pre_app_specialize;
    zygisk_post_app_specialize_fn post_app_specialize;
    zygisk_pre_server_specialize_fn pre_server_specialize;
    zygisk_post_server_specialize_fn post_server_specialize;
};

/* No hooks; only preload core libs to widen symbol visibility on pre-9 linker. */
static void preload_core_libs(void) {
    const char *libs32[] = {
        "/system/lib/libnativehelper.so",
        "/system/lib/libandroid_runtime.so",
    };
    const char *libs64[] = {
        "/system/lib64/libnativehelper.so",
        "/system/lib64/libandroid_runtime.so",
    };

#if defined(__LP64__)
    const char **libs = libs64;
    size_t lib_count = sizeof(libs64)/sizeof(libs64[0]);
#else
    const char **libs = libs32;
    size_t lib_count = sizeof(libs32)/sizeof(libs32[0]);
#endif

    for (size_t i = 0; i < lib_count; ++i) {
        void *h = dlopen(libs[i], RTLD_NOW | RTLD_GLOBAL);
        if (!h) {
            LOGW("dlopen %s failed: %s", libs[i], dlerror());
        } else {
            LOGD("dlopen %s ok", libs[i]);
        }
    }
}

static void on_load(struct zygisk_ctx *ctx) {
    (void)ctx;
    preload_core_libs();
}

static void pre_app(struct zygisk_ctx *ctx)   { (void)ctx; }
static void post_app(struct zygisk_ctx *ctx)  { (void)ctx; }
static void pre_sys(struct zygisk_ctx *ctx)   { (void)ctx; }
static void post_sys(struct zygisk_ctx *ctx)  { (void)ctx; }

__attribute__((visibility("default")))
struct zygisk_module_v3 zygisk_module = {
    .version = 3,
    .name = "zygisk_stabilizer",
    .on_load = on_load,
    .pre_app_specialize = pre_app,
    .post_app_specialize = post_app,
    .pre_server_specialize = pre_sys,
    .post_server_specialize = post_sys,
};
