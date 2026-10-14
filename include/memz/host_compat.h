#ifndef _MEMZ_HOST_COMPAT_H
#define _MEMZ_HOST_COMPAT_H

/*
 * Win32 look-alikes for the *host* (non Windows) build of src/cache.c.
 *
 * The cache layer is platform guarded so that it can be compiled, unit
 * tested and benchmarked on the machine that builds memz (Linux/macOS),
 * instead of only being verifiable on a Windows box. This header is what
 * makes that possible. It is NEVER part of the Windows build: src/cache.c
 * only includes it when _WIN32 is not defined.
 *
 * It is pulled in by <memz/cache.h>, never by hand.
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Minimal subset of the Win32 types used by the cache API. */
typedef int             BOOL;
typedef unsigned long   DWORD;
typedef void           *HWND;
typedef void           *HICON;
typedef char           *LPSTR;
typedef const char     *LPCSTR;
typedef wchar_t         WCHAR;
typedef WCHAR          *LPWSTR;
typedef const WCHAR    *LPCWSTR;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/* lstrlenW() is just wcslen() with the Win32 spelling. */
#define lstrlenW(s) ((int)wcslen((const wchar_t *)(s)))

/*
 * Supplied by the host harness (tools/bench/bench.c). These stand in for
 * GetSystemMetrics() and LoadIcon() so the benchmark can count how many
 * real API calls the cache saves.
 */
int   host_metric(int index);
void *host_icon(const char *id);

#endif // _MEMZ_HOST_COMPAT_H
