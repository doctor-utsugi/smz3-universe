/* The few POSIX calls the PC frontend uses that Windows spells differently. */
#ifndef PC_COMPAT_H
#define PC_COMPAT_H

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <windows.h>
#ifndef F_OK
#define F_OK 0
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define mkdir(p, m) _mkdir(p)
#define access(p, m) _access(p, m)
#define realpath(p, out) _fullpath(out, p, PATH_MAX)
#include <time.h>
#define gmtime_r(t, out) (gmtime_s(out, t) ? NULL : (out))
#define timegm(tm) _mkgmtime(tm)
#else
#include <limits.h>
#include <unistd.h>
#endif

#endif
