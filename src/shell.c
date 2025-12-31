#include <stdio.h>

#include "dagwood.h"

#if defined(_WIN32) || defined(__CYGWIN__) || defined(_MSC_VER)
#define DEFAULT_SHELL "cmd"
#define DEFAULT_SHELL_ARG "/c"
#else
#define DEFAULT_SHELL "/bin/sh"
#define DEFAULT_SHELL_ARG "-c"
#endif

inline const char *dagwood_platform_shell(void) {
  return DEFAULT_SHELL;
}

inline const char *dagwood_platform_shell_arg(void) {
  return DEFAULT_SHELL_ARG;
}
