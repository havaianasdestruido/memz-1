#ifndef _MEMZ_CACHE_H
#define _MEMZ_CACHE_H

/*
 * memz RAM cache
 * ==============
 *
 * Every payload below is a loop that recomputes the same values over and
 * over again, thousands of times per second:
 *
 *   random()                one CryptGenRandom() *syscall* per number
 *   buzzer()                1 323 000 float divisions per 30 s buffer, forever
 *   invert_text()           EnumChildWindows + 2 cross process SendMessage per
 *                           window, plus a 16 KB GlobalAlloc/GlobalFree pair
 *                           per window, every 2 seconds, forever
 *   draw_icons()            GetSystemMetrics() + LoadIcon() on every tick
 *   overwatch_thread()      OpenProcess() + GetProcessImageFileNameA() for
 *                           *every* process in the system, every 10 ms
 *
 * None of those results change often enough to justify recomputing them.
 * This module computes each of them once and keeps the result in RAM, so
 * the CPU only pays for a calculation the first time it is needed.
 *
 * Everything here is a pure cache: with MEMZ_CACHE_AGRESSIVE = 0 the byte
 * stream produced by the payloads is identical to the uncached code, only
 * cheaper. See docs/CACHING.md for the full breakdown, the memory cost and
 * the measured speedups.
 */

#include <memz/def.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <memz/host_compat.h>   /* host only shims, used by tools/bench */
#endif

#include <stddef.h>

/* ==================================================================== */
/* Tunables -- override any of these on the compiler command line.      */
/* ==================================================================== */

/*
 * How many 32 bit random values are kept in RAM. A single CryptGenRandom()
 * call refills the whole pool, so the syscall cost is amortised over this
 * many draws. 65536 values = 256 KB.
 */
#ifndef MEMZ_CACHE_RANDOM_SLOTS
#define MEMZ_CACHE_RANDOM_SLOTS   (1u << 16)
#endif

/*
 * 1 = trade a little fidelity for a lot of CPU:
 *       - 30 second audio buffers are pre-rendered once and replayed from
 *         RAM instead of being synthesised on every pass,
 *       - windows whose text is already the reversed string we sent last
 *         time are skipped (no WM_GETTEXT/WM_SETTEXT round trip),
 *       - the child window list is enumerated every few ticks instead of
 *         on every tick.
 * 0 = only memoize computations whose result is bit identical to the
 *     uncached code (audio is still ~50x cheaper, just not free).
 */
#ifndef MEMZ_CACHE_AGRESSIVE
#define MEMZ_CACHE_AGRESSIVE      1
#endif

/* Pre-rendered 30 second PCM buffers kept in RAM (1 322 880 bytes each). */
#ifndef MEMZ_CACHE_WAVE_BUFFERS
#define MEMZ_CACHE_WAVE_BUFFERS   16
#endif

/* Ram cached windows (HWND, both the text and its reversal). */
#ifndef MEMZ_CACHE_TEXT_SLOTS
#define MEMZ_CACHE_TEXT_SLOTS     512
#endif

/* Longest window text that gets memoized (longer ones are recomputed). */
#ifndef MEMZ_CACHE_TEXT_CHARS
#define MEMZ_CACHE_TEXT_CHARS     512
#endif

/* Cached child windows, and how many ticks a cached list stays valid. */
#ifndef MEMZ_CACHE_CHILD_SLOTS
#define MEMZ_CACHE_CHILD_SLOTS    256
#endif
#ifndef MEMZ_CACHE_CHILD_REFRESH
#define MEMZ_CACHE_CHILD_REFRESH  4
#endif

/*
 * How many payload ticks a window that already holds a reversed title is
 * left alone before it is looked at again. 1 = look at every window on every
 * tick (the original behaviour). Aggressive mode uses 2: invert_text() runs
 * every 2 seconds, so a changed title is still picked up within 4 seconds.
 */
#ifndef MEMZ_CACHE_TEXT_TICKS
#if MEMZ_CACHE_AGRESSIVE
#define MEMZ_CACHE_TEXT_TICKS     2
#else
#define MEMZ_CACHE_TEXT_TICKS     1
#endif
#endif

/* Slots in the pid -> "is this memz?" lookup table. */
#ifndef MEMZ_CACHE_PROC_SLOTS
#define MEMZ_CACHE_PROC_SLOTS     2048
#endif

/* Slots for GetSystemMetrics() / LoadIcon() / system sound caches. */
#ifndef MEMZ_CACHE_METRIC_SLOTS
#define MEMZ_CACHE_METRIC_SLOTS   16
#endif
#ifndef MEMZ_CACHE_ICON_SLOTS
#define MEMZ_CACHE_ICON_SLOTS     8
#endif
#ifndef MEMZ_CACHE_SOUND_SLOTS
#define MEMZ_CACHE_SOUND_SLOTS    8
#endif

/* ==================================================================== */
/* Waveform constants (the buzzer payload).                             */
/* ==================================================================== */

#define MEMZ_WAVE_RATE     44100                       /* samples / second  */
#define MEMZ_WAVE_SECONDS  30                          /* buffer length     */
#define MEMZ_WAVE_BYTES    (MEMZ_WAVE_RATE * MEMZ_WAVE_SECONDS)
#define MEMZ_WAVE_SEGMENT  (MEMZ_WAVE_RATE / 4)        /* freq changes 4x/s */
#define MEMZ_WAVE_FREQ_MAX 64                          /* 44100 / 1000 = 44 */

/* ==================================================================== */
/* Statistics -- every counter is "number of times we did NOT have to   */
/* ask the OS / recompute something".                                   */
/* ==================================================================== */

typedef struct memz_cache_stats
{
    unsigned long long random_draws;    /* numbers served out of RAM        */
    unsigned long long random_refills;  /* CryptGenRandom() calls           */
    unsigned long long metric_hits;     /* GetSystemMetrics() avoided       */
    unsigned long long metric_misses;   /* GetSystemMetrics() executed      */
    unsigned long long icon_hits;       /* LoadIcon() avoided               */
    unsigned long long icon_misses;     /* LoadIcon() executed              */
    unsigned long long sound_hits;      /* registry + disk reads avoided    */
    unsigned long long sound_misses;    /* registry + disk reads executed   */
    unsigned long long text_hits;       /* string reversals avoided         */
    unsigned long long text_settled;    /* WM_SETTEXT round trips avoided   */
    unsigned long long text_misses;     /* string reversals executed        */
    unsigned long long proc_hits;       /* OpenProcess()/GetImageFile calls */
    unsigned long long proc_misses;     /* ... that were actually needed    */
    unsigned long long wave_renders;    /* 30 s buffers synthesised         */
    unsigned long long wave_reuses;     /* 30 s buffers served from RAM     */
    size_t             bytes;           /* resident cache footprint         */
} memz_cache_stats;

/* ==================================================================== */
/* Lifecycle                                                            */
/* ==================================================================== */

/* Idempotent, thread safe, cheap to call twice. Safe to call from every
 * entry point; nothing below requires it, everything works lazily too. */
void cache_init(void);

/* Frees every cached block. Only needed if the process cleans up after
 * itself -- memz normally does not. */
void cache_shutdown(void);

void cache_get_stats(memz_cache_stats *out);
void cache_dump_stats(void);

/* ==================================================================== */
/* Cached randomness                                                    */
/* ==================================================================== */

/* Same range and distribution as the old random(): [0, 0x7fffffff]. */
int cache_random(void);

/* Uniform value in [0, upper) without an integer division
 * (multiply + shift instead of idiv, unbiased via rejection). */
int cache_random_below(int upper);

/* ==================================================================== */
/* Cached system values                                                 */
/* ==================================================================== */

/* Memoized GetSystemMetrics(index). Screen metrics do not change while
 * memz is running, so they are resolved exactly once. */
int   cache_metric(int index);
void  cache_metric_invalidate(void);

/* Memoized LoadIconA(NULL, id). System icons are shared handles, there is
 * no reason to ask for them again on every tick. */
HICON cache_icon(LPCSTR id);

/*
 * Memoized system sound: resolves the alias ("SystemHand", ...) through the
 * registry once, reads the .wav into RAM and hands back a pointer that can
 * be fed straight to PlaySoundA(..., SND_MEMORY). Returns NULL if the sound
 * cannot be resolved, in which case the caller should fall back to asking
 * Windows to play the alias itself.
 */
const void *cache_sound(LPCSTR alias, size_t *size);

/* ==================================================================== */
/* Cached text transformation (invert_text payload)                     */
/* ==================================================================== */

typedef enum memz_text_state
{
    MEMZ_TEXT_UNCHANGED = 0,  /* the window already holds our last result,
                               * there is nothing to send (aggressive only) */
    MEMZ_TEXT_CACHED,         /* result rebuilt from RAM, out is filled     */
    MEMZ_TEXT_COMPUTE         /* unknown text, caller must compute it and
                               * then hand it back with cache_text_put()    */
} memz_text_state;

/*
 * Each of these is tied to a payload tick: cache_text_tick() is called once
 * per invert_text() run, cache_text_due() says whether a window is worth
 * talking to at all this tick, and cache_text_get()/cache_text_put() do the
 * memoized round trip.
 *
 * `chars` is the length of `text` in WCHARs. SendMessageTimeout(WM_GETTEXT)
 * returns exactly that number, so it is free.
 *
 * On MEMZ_TEXT_CACHED `out` already holds the result -- the caller just has
 * to send it. On MEMZ_TEXT_COMPUTE the caller computes it, sends it, and
 * hands it back with cache_text_put() so the next tick gets a hit.
 */
void            cache_text_tick(void);
int             cache_text_due(HWND hwnd);
memz_text_state cache_text_get(HWND hwnd, const WCHAR *text, int chars,
                               WCHAR *out, size_t out_chars);
void            cache_text_put(HWND hwnd, const WCHAR *text, int chars,
                               const WCHAR *result, int result_chars);
void            cache_text_reset(void);

/* ==================================================================== */
/* Cached process scan (overwatch watchdog)                             */
/* ==================================================================== */

/* Called on a cache miss only. Must return non zero if `pid` runs the
 * same image file as the current process. `self` is cache_self_image(). */
typedef int (*cache_proc_resolve_fn)(DWORD pid, const char *self, void *ctx);

/* The image file name of the current process, resolved once. */
const char *cache_self_image(void);

/* Wrap one process snapshot with these two calls: cache_proc_begin() bumps
 * the generation counter, cache_proc_end() drops entries for pids that are
 * no longer running. */
void cache_proc_begin(void);
void cache_proc_end(void);

/* 1 = same image as us, 0 = not, -1 = unknown and no resolver given. */
int  cache_proc_match(DWORD pid, cache_proc_resolve_fn resolve, void *ctx);

/* ==================================================================== */
/* Cached waveform (buzzer payload)                                     */
/* ==================================================================== */

/* Memoized 44100 / (r + 1000), the buzzer's frequency division. */
int   cache_wave_freq(int r);

/* Fills `samples` bytes of the ramp for `freq`, starting at `phase`, by
 * memcpy()ing from a pre-computed cycle table. Produces exactly the same
 * bytes as the original per sample float division. */
void  cache_wave_fill(char *dst, int samples, int freq, int phase);

/* Renders a whole 30 second buffer from the cycle tables (bit identical to
 * the original generator, roughly two orders of magnitude cheaper). */
void  cache_wave_render(char *dst);

/* Pre-rendered 30 second buffer from the RAM pool (index wraps around).
 * Rendered on first use, replayed forever after. NULL when out of memory. */
char *cache_wave_buffer(unsigned index);

#endif // _MEMZ_CACHE_H
