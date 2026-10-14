#include <memz/util.h>
#include <memz/cache.h>

/*
 * random() is the most called function in the whole program: every payload
 * tick asks for one to five numbers, and the buzzer asks for 120 per audio
 * buffer. It used to be one CryptGenRandom() *syscall* per number.
 *
 * The cache keeps a pool of MEMZ_CACHE_RANDOM_SLOTS numbers in RAM and
 * refills the whole pool with a single CryptGenRandom() call, so the syscall
 * cost is amortised over 65536 draws. The values themselves are still
 * cryptographically random and cover the same range as before.
 */

int random()
{
    return cache_random();
}

/*
 * Cheaper range reduction for the hot paths. `random() % upper` compiles to
 * an integer division (~20-40 cycles); this is a multiply and a shift with a
 * rejection step that almost never triggers. Use it wherever the old code
 * wrote `random() % n`.
 */
int random_below(int upper)
{
    return cache_random_below(upper);
}

void reverse_string_w(LPWSTR str)
{
    int len = lstrlenW(str);

    if (len <= 1)
        return;

    WCHAR c;
    int i, j;
    for (i = 0, j = len - 1; i < j; i++, j--) {
        c = str[i];
        str[i] = str[j];
        str[j] = c;
    }

    // Fix Newlines
    for (i = 0; i < len - 1; i++) {
        if (str[i] == L'\n' && str[i + 1] == L'\r') {
            str[i] = L'\r';
            str[i + 1] = L'\n';
        }
    }
}

LRESULT CALLBACK message_box_hook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_CREATEWND) {
        CREATESTRUCT *pcs = ((CBT_CREATEWND *)lParam)->lpcs;

        if ((pcs->style & WS_DLGFRAME) || (pcs->style & WS_POPUP)) {
            //HWND hwnd = (HWND)wParam;

            /* Screen metrics do not change while memz runs: resolved once. */
            int x = random_below(cache_metric(SM_CXSCREEN) - pcs->cx);
            int y = random_below(cache_metric(SM_CYSCREEN) - pcs->cy);

            pcs->x = x;
            pcs->y = y;
        }
    }

    return CallNextHookEx(0, nCode, wParam, lParam);
}
