/* win_compat.h — the POSIX calls the sim uses that MinGW (MSYS2 UCRT64) lacks.
 * Force-included by the Makefile on Windows only; the other builds never see it. */
#ifndef WIN_COMPAT_H
#define WIN_COMPAT_H
#ifdef _WIN32
#include <stdlib.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>

/* every caller overwrites (setenv(..., 1)); an empty value unsets on Windows */
static inline int setenv(const char *name, const char *value, int overwrite) { (void)overwrite; return _putenv_s(name, value); }
static inline int unsetenv(const char *name) { return _putenv_s(name, ""); }
/* Windows' mkdir takes no mode (the headers above declared it first) */
#define mkdir(dir, mode) _mkdir(dir)
#endif
#endif
