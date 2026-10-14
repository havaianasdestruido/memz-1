#ifndef _MEMZ_WINDOW_H
#define _MEMZ_WINDOW_H

#include <memz/def.h>
#include <Windows.h>

typedef struct
{
    HWND hwnd;
    HDC hdc;
    RECT rect;

    /* Cached geometry: rect only changes if the resolution does, and the
     * payload loops used to subtract right-left / bottom-top on every single
     * tick. Resolved once in payload_host(). */
    int width;
    int height;
} window_t;

#endif // _MEMZ_WINDOW_H
