#ifndef ART_METHOD_H
#define ART_METHOD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <jni.h>

#include "logging.h"

static jfieldID art_method_field = NULL;
static size_t art_method_size = 0;
static size_t entry_point_offset = 0;
static size_t data_offset = 0;

static inline void *amethod_from_reflected_method(JNIEnv *env, jobject method);

/*
  INFO: Inlining these methods to ensure multiple definitions, avoiding ODR violations.
          Check module.h for more info.

  SOURCES:
   - https://clang.llvm.org/extra/clang-tidy/checks/misc/definitions-in-headers.html
*/

static inline bool amethod_init(JNIEnv *env) {
  jclass clazz = (*env)->FindClass(env, "java/lang/reflect/Executable");
  if (!clazz) {
    LOGE("Failed to found Executable");

    return false;
  }

  art_method_field = (*env)->GetFieldID(env, clazz, "artMethod", "J");
  if (!art_method_field) {
    LOGE("Failed to find artMethod field");

    (*env)->DeleteLocalRef(env, clazz);

    return false;
  }

  jclass throwable = (*env)->FindClass(env, "java/lang/Throwable");
  if (!throwable) {
    LOGE("Failed to found Throwable");

    (*env)->DeleteLocalRef(env, clazz);

    return false;
  }

  jclass clz = (*env)->FindClass(env, "java/lang/Class");
  if (!clz) {
    LOGE("Failed to found Class");

    (*env)->DeleteLocalRef(env, clazz);
    (*env)->DeleteLocalRef(env, throwable);

    return false;
  }

  jmethodID get_declared_constructors = (*env)->GetMethodID(env, clz, "getDeclaredConstructors", "()[Ljava/lang/reflect/Constructor;");
  (*env)->DeleteLocalRef(env, clz);

  jobjectArray constructors = (jobjectArray)(*env)->CallObjectMethod(env, throwable, get_declared_constructors);
  (*env)->DeleteLocalRef(env, throwable);
  if (!constructors || (*env)->GetArrayLength(env, constructors) < 2) {
    LOGE("Throwable has less than 2 constructors");

    (*env)->DeleteLocalRef(env, clazz);

    return false;
  }

  jobject first_ctor = (*env)->GetObjectArrayElement(env, constructors, 0);
  jobject second_ctor = (*env)->GetObjectArrayElement(env, constructors, 1);

  uintptr_t first = (uintptr_t)amethod_from_reflected_method(env, first_ctor);
  uintptr_t second = (uintptr_t)amethod_from_reflected_method(env, second_ctor);

  (*env)->DeleteLocalRef(env, first_ctor);
  (*env)->DeleteLocalRef(env, second_ctor);
  (*env)->DeleteLocalRef(env, constructors);
  (*env)->DeleteLocalRef(env, clazz);

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

static inline void *amethod_get_data(uintptr_t self) {
  return *(void **)(self + data_offset);
}

static inline void *amethod_from_reflected_method(JNIEnv *env, jobject method) {
  if (art_method_field) {
    return (void *)(*env)->GetLongField(env, method, art_method_field);
  } else {
    return (void *)(*env)->FromReflectedMethod(env, method);
  }
}

#endif /* ART_METHOD_H */
