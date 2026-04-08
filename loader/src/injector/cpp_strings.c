/* INFO: C implementation for reading C++ std::string from memory.
           Handles libc++ std::string layout with SSO (Small String Optimization).

         libc++ layout:
          - Short mode (LSB of first byte = 0): size = first_byte >> 1, data at byte 1
          - Long mode: capacity/size/pointer at platform-specific offsets
*/

#include "cpp_strings.h"

#ifdef __LP64__
  #define LONG_SIZE_OFFSET 8
  #define LONG_DATA_OFFSET 16
#else
  #define LONG_SIZE_OFFSET 4
  #define LONG_DATA_OFFSET 8
#endif

/* INFO: In libc++ little-endian: LSB of first byte = 0 means short mode */
static inline bool is_short_string(const unsigned char *bytes) {
  return (bytes[0] & 1) == 0;
}

size_t get_std_string_length(const void *std_string_ptr) {
  if (!std_string_ptr) return 0;

  const unsigned char *bytes = (const unsigned char *)std_string_ptr;

  if (is_short_string(bytes)) return bytes[0] >> 1;

  return *(const size_t *)((const char *)std_string_ptr + LONG_SIZE_OFFSET);
}

const char *read_std_string(const void *std_string_ptr) {
  if (!std_string_ptr) return NULL;

  const unsigned char *bytes = (const unsigned char *)std_string_ptr;

  if (is_short_string(bytes)) return (const char *)(bytes + 1);

  return *(const char **)((const char *)std_string_ptr + LONG_DATA_OFFSET);
}
