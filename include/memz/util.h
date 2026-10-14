#ifndef _MEMZ_UTIL_H
#define _MEMZ_UTIL_H

#include <memz/def.h>
#include <Windows.h>

/* Both are served from the RAM cache, see src/cache.c. */
int random();
int random_below(int upper);   /* uniform [0, upper), no integer division */

void reverse_string_w(LPWSTR str);
LRESULT CALLBACK message_box_hook(int nCode, WPARAM wParam, LPARAM lParam);

#endif // _MEMZ_UTIL_H