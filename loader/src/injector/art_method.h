#ifndef ART_METHOD_H
#define ART_METHOD_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <jni.h>

#include "logging.h"

static jfieldID art_method_field = NULL;
static size_t art_method_size = 0;
static size_t entry_point_offset = 0;
static size_t data_offset = 0;

void *amethod_from_reflected_method(JNIEnv *env, jobject method);

/*
  INFO: Inlining these methods to ensure multiple definitions, avoiding ODR violations.
          Check module.h for more info.

  SOURCES:
   - https://clang.llvm.org/extra/clang-tidy/checks/misc/definitions-in-headers.html
*/

inline bool amethod_init(JNIEnv *env) {
  jclass clazz = env->FindClass("java/lang/reflect/Executable");
  if (!clazz) {
    LOGE("Failed to found Executable");

    return false;
  }

  if (!(art_method_field = env->GetFieldID(clazz, "artMethod", "J"))) {
    LOGE("Failed to find artMethod field");

    env->DeleteLocalRef(clazz);

    return false;
  }

  jclass throwable = env->FindClass("java/lang/Throwable");
  if (!throwable) {
    LOGE("Failed to found Executable");

    env->DeleteLocalRef(clazz);

    return false;
  }

  jclass clz = env->FindClass("java/lang/Class");
  if (!clz) {
    LOGE("Failed to found Class");

    env->DeleteLocalRef(clazz);
    env->DeleteLocalRef(throwable);

    return false;
  }

  jmethodID get_declared_constructors = env->GetMethodID(clz, "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;");
  env->DeleteLocalRef(clz);

  const auto constructors = (jobjectArray) env->CallObjectMethod(throwable, get_declared_constructors);
  env->DeleteLocalRef(throwable);
  if (!constructors || env->GetArrayLength(constructors) < 2) {
    LOGE("Throwable has less than 2 constructors");

    env->DeleteLocalRef(clazz);

    return false;
  }

  jobject first_ctor = env->GetObjectArrayElement(constructors, 0);
  jobject second_ctor = env->GetObjectArrayElement(constructors, 1);

  uintptr_t first = (uintptr_t)amethod_from_reflected_method(env, first_ctor);
  uintptr_t second = (uintptr_t)amethod_from_reflected_method(env, second_ctor);

  env->DeleteLocalRef(first_ctor);
  env->DeleteLocalRef(second_ctor);
  env->DeleteLocalRef(constructors);

  art_method_size = second - first;
  LOGD("ArtMethod size: %zu", art_method_size);
  if ((4 * 9 + 3 * sizeof(void *)) < art_method_size) {
    LOGE("ArtMethod size exceeds maximum assume. There may be something wrong.");

    return false;
  }

  entry_point_offset = art_method_size - sizeof(void *);
  data_offset = entry_point_offset - sizeof(void *);
  LOGD("ArtMethod entrypoint offset: %zu", entry_point_offset);
  LOGD("ArtMethod data offset: %zu", data_offset);

  return true;
}

inline void *amethod_get_data(uintptr_t self) {
  return *(void **)(self + data_offset);
}

inline void *amethod_from_reflected_method(JNIEnv *env, jobject method) {
  if (art_method_field) {
    return (void *)env->GetLongField(method, art_method_field);
  } else {
    return (void *)env->FromReflectedMethod(method);
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ART_METHOD_H */
