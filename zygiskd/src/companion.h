#ifndef __COMPANION_H__
#define __COMPANION_H__

#include <stddef.h>

// Define the zygisk companion entry point function type
typedef void (*zygisk_companion_entry)(int);

// Main entry point for companion module
void companion_entry(int fd);

#endif /* __COMPANION_H__ */
