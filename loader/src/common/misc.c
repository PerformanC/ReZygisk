#include <stdio.h>
#include <sys/utsname.h>

#include "logging.h"

#include "misc.h"

int parse_int(const char *str) {
  int val = 0;

  const char *c = str;
  while (*c) {
    if (*c > '9' || *c < '0')
      return -1;

    val = val * 10 + *c - '0';
    c++;
  }

  return val;
}

struct kernel_version parse_kversion() {
  struct utsname uts;
  if (uname(&uts) == -1) {
    PLOGE("uname");

    return (struct kernel_version) { 0 };
  }

  struct kernel_version version;
  sscanf(uts.release, "%hhu.%u.%u", &version.major, &version.minor, &version.patch);

  return version;
}
