#ifndef SHELL_H
#define SHELL_H

#if defined(_WIN32) || defined(__CYGWIN__)
#define DEFAULT_SHELL "cmd /c"
#else
#define DEFAULT_SHELL "/bin/sh -c"
#endif

#endif
