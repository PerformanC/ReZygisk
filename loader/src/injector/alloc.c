/* INFO: Heap strings may still persist after freed. Those strings
           could be abused to detect ReZygisk. This code memsets those
           strings before it is freed, so it kills those detections.
           
         Those are called due to -Wl,--wrap=free -Wl,--wrap=realloc
           in Makefile/compilation, which makes the linker replace free()
           and realloc() with __wrap_free() and __wrap_realloc().
*/

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

void __real_free(void *ptr);

void __wrap_free(void *ptr) {
  if (!ptr) return;

  size_t usable = malloc_usable_size(ptr);
  if (usable > 0) {
    memset(ptr, 0, usable);

    /* INFO: Avoid compiler optimizing out the memset. */
    __asm__ volatile("" : : "r"(ptr) : "memory");
  }

  __real_free(ptr);
}

void *__wrap_realloc(void *ptr, size_t new_size) {
  if (!ptr) return malloc(new_size);

  if (new_size == 0) {
    __wrap_free(ptr);

    return NULL;
  }

  size_t usable = malloc_usable_size(ptr);
  if (new_size <= usable) return ptr;

  void *new_ptr = malloc(new_size);
  if (!new_ptr) return NULL;

  memcpy(new_ptr, ptr, usable);

  memset(ptr, 0, usable);

  /* INFO: Avoid compiler optimizing out the memset. */
  __asm__ volatile("" : : "r"(ptr) : "memory");
  __real_free(ptr);

  return new_ptr;
}
