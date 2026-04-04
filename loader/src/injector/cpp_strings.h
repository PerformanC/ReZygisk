#ifndef CPP_STRINGS_H
#define CPP_STRINGS_H

#include <stdbool.h>
#include <stddef.h>

/* INFO: Read a C string pointer from a std::string object.
           The returned pointer is valid only as long as the std::string exists. */
const char *read_std_string(const void *std_string_ptr);

/* INFO: Get the length of a std::string object (not including null terminator). */
size_t get_std_string_length(const void *std_string_ptr);

#endif /* CPP_STRINGS_H */
