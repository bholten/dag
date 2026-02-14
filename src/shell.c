#define _GNU_SOURCE

#include <spawn.h>
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

int dagwood_spawn_addchdir(posix_spawn_file_actions_t *actions,
                           const char *path) {
#ifdef HAVE_SPAWN_ADDCHDIR_NP
  return posix_spawn_file_actions_addchdir_np(actions, path);
#else
  (void)actions;
  (void)path;
  fprintf(stderr,
          "[dagwood] working directory not supported on this platform\n");
  return -1;
#endif
}
