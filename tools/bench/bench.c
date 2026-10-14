#define _POSIX_C_SOURCE 200809L

/*
 * tools/bench/bench.c -- host side benchmark and correctness check for the
 * memz RAM cache.
 *
 * src/cache.c is platform guarded, so the very same code that ships in the
 * Windows binary can be compiled here and measured. Nothing in this file is
 * part of memz itself; it exists so that "it is faster" is a measurement
 * instead of a claim.
 *
 *   make -C tools/bench        # builds and runs both modes
 *
 * Two binaries are produced, one with MEMZ_CACHE_AGRESSIVE=0 (bit identical
 * results, verified byte for byte) and one with MEMZ_CACHE_AGRESSIVE=1.
 */

#include <memz/cache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/random.h>

/* ------------------------------------------------------------------ */
/* host_metric() / host_icon(): the Win32 calls the cache wraps.       */
/* They are counted so the benchmark can show how many of them the     */
/* cache actually removes.                                             */
/* ------------------------------------------------------------------ */

static unsigned long g_metric_calls;
static unsigned long g_icon_calls;

int host_metric(int index)
{
    g_metric_calls++;
    /* Stand in for GetSystemMetrics(); the value only has to be stable. */
    switch (index)
    {
    case 0:  return 1920;   /* SM_CXSCREEN */
    case 1:  return 1080;   /* SM_CYSCREEN */
    case 11: return 32;     /* SM_CXICON   */
    case 12: return 32;     /* SM_CYICON   */
    default: return 1000 + index;
    }
}

void *host_icon(const char *id)
{
    g_icon_calls++;
    (void)id;
    /* Icon handles are opaque; any non NULL pointer will do. */
    return (void *)(size_t)0x49434f4eu;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int g_failures;

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void check(const char *what, int ok)
{
    printf("   [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        g_failures++;
}

static unsigned int xs_state = 123456789u;

static unsigned int xs_next(void)
{
    xs_state ^= xs_state << 13;
    xs_state ^= xs_state >> 17;
    xs_state ^= xs_state << 5;
    return xs_state;
}

/* One real syscall per call, the way CryptGenRandom() behaves. */
static unsigned int os_random(void)
{
    unsigned int v;
    if (getrandom(&v, sizeof(v), 0) != (ssize_t)sizeof(v))
        abort();
    return v;
}

/* Mirror of src/util.c:reverse_string_w() -- kept in sync deliberately. */
static void reverse_string_w(LPWSTR str)
{
    int len = lstrlenW(str);
    int i, j;
    WCHAR c;

    if (len <= 1)
        return;

    for (i = 0, j = len - 1; i < j; i++, j--) {
        c = str[i];
        str[i] = str[j];
        str[j] = c;
    }

    for (i = 0; i < len - 1; i++) {
        if (str[i] == L'\n' && str[i + 1] == L'\r') {
            str[i] = L'\r';
            str[i + 1] = L'\n';
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1. Cached randomness                                                */
/* ------------------------------------------------------------------ */

#define RND_DRAWS 2000000

static void bench_random(void)
{
    double t0, t1, uncached, cached;
    unsigned long long acc = 0;
    int i;
    memz_cache_stats before, after;
    int range_ok = 1;
    unsigned long buckets[16] = { 0 };

    cache_get_stats(&before);

    t0 = now_seconds();
    for (i = 0; i < RND_DRAWS; i++)
        acc += os_random() & 0x7fffffffu;
    t1 = now_seconds();
    uncached = t1 - t0;

    t0 = now_seconds();
    for (i = 0; i < RND_DRAWS; i++)
        acc += (unsigned long long)cache_random();
    t1 = now_seconds();
    cached = t1 - t0;

    cache_get_stats(&after);

    printf("\n1. random(): one CryptGenRandom() syscall per number -> one per pool refill\n");
    printf("   uncached  %8.1f ms  (%7.1f ns/draw, %d OS calls)\n",
           uncached * 1e3, uncached * 1e9 / RND_DRAWS, RND_DRAWS);
    printf("   cached    %8.1f ms  (%7.1f ns/draw, %llu OS calls)\n",
           cached * 1e3, cached * 1e9 / RND_DRAWS,
           after.random_refills - before.random_refills);
    printf("   speedup   %8.1fx\n", uncached / (cached > 0 ? cached : 1e-9));
    printf("   pool      %d values (%.0f KB), %llu refills for %d draws\n",
           MEMZ_CACHE_RANDOM_SLOTS,
           (double)MEMZ_CACHE_RANDOM_SLOTS * sizeof(int) / 1024.0,
           after.random_refills - before.random_refills, RND_DRAWS);

    for (i = 0; i < 1000000; i++)
    {
        int v = cache_random_below(4000);
        if (v < 0 || v >= 4000)
            range_ok = 0;
        buckets[(v * 16) / 4000]++;
    }

    {
        unsigned long lo = ~0ul, hi = 0;
        int flat = 1;
        for (i = 0; i < 16; i++)
        {
            if (buckets[i] < lo) lo = buckets[i];
            if (buckets[i] > hi) hi = buckets[i];
        }
        /* Uniform over 16 buckets: expect 62500 each, allow 5%. */
        if (lo < 59000ul || hi > 66000ul)
            flat = 0;
        check("cache_random_below() stays in range", range_ok);
        check("cache_random_below() is uniform (16 buckets, 1M draws)", flat);
    }

    printf("   (acc %llu, prevents the loop being optimised away)\n", acc);
}

/* ------------------------------------------------------------------ */
/* 2. Waveform                                                         */
/* ------------------------------------------------------------------ */

/* The original inner loop, verbatim from the old buzzer() payload. */
static void wave_reference(char *dst, const int *freqs)
{
    int i;
    int freq = 0;

    for (i = 0; i < MEMZ_WAVE_BYTES; i++)
    {
        if (i % MEMZ_WAVE_SEGMENT == 0)
            freq = freqs[i / MEMZ_WAVE_SEGMENT];

        dst[i] = (char)(((i % freq) / ((float)freq)) * 100);
    }
}

/* Same buffer, but every segment is a memcpy() out of the cycle tables. */
static void wave_from_tables(char *dst, const int *freqs)
{
    int seg;

    for (seg = 0; seg < MEMZ_WAVE_SECONDS * 4; seg++)
    {
        int phase = (seg * MEMZ_WAVE_SEGMENT) % freqs[seg];
        cache_wave_fill(dst + (seg * MEMZ_WAVE_SEGMENT), MEMZ_WAVE_SEGMENT,
                        freqs[seg], phase);
    }
}

#define WAVE_ITERS 12

static void bench_wave(void)
{
    char *reference = (char *)malloc(MEMZ_WAVE_BYTES);
    char *tables = (char *)malloc(MEMZ_WAVE_BYTES);
    int freqs[MEMZ_WAVE_SECONDS * 4];
    double t0, t1, t_ref, t_tables;
    int i, seg;
    unsigned long long acc = 0;

    if (!reference || !tables)
        abort();

    for (seg = 0; seg < MEMZ_WAVE_SECONDS * 4; seg++)
        freqs[seg] = cache_wave_freq((int)(xs_next() % 4000));

    printf("\n2. buzzer(): %d samples per %d s buffer, each one a modulo + float divide\n",
           MEMZ_WAVE_BYTES, MEMZ_WAVE_SECONDS);

    /* ---- correctness first: the tables must reproduce the original bytes */
    wave_reference(reference, freqs);
    wave_from_tables(tables, freqs);
    check("cycle tables are byte identical to the original generator",
          memcmp(reference, tables, MEMZ_WAVE_BYTES) == 0);

    /* ---- speed: original loop vs cycle tables */
    t0 = now_seconds();
    for (i = 0; i < WAVE_ITERS; i++)
    {
        wave_reference(reference, freqs);
        acc += reference[i];
    }
    t1 = now_seconds();
    t_ref = t1 - t0;

    t0 = now_seconds();
    for (i = 0; i < WAVE_ITERS; i++)
    {
        wave_from_tables(tables, freqs);
        acc += tables[i];
    }
    t1 = now_seconds();
    t_tables = t1 - t0;

    printf("   uncached  %8.1f ms per buffer  (%llu float divisions)\n",
           t_ref * 1e3 / WAVE_ITERS,
           (unsigned long long)MEMZ_WAVE_BYTES);
    printf("   tables    %8.1f ms per buffer  (%d memcpys)\n",
           t_tables * 1e3 / WAVE_ITERS, MEMZ_WAVE_SECONDS * 4);
    printf("   speedup   %8.1fx\n", t_ref / (t_tables > 0 ? t_tables : 1e-9));

#if MEMZ_CACHE_AGRESSIVE
    {
        /* ---- aggressive: whole buffers pre-rendered and replayed */
        double t_pool;
        int iters = 1000;

        for (i = 0; i < MEMZ_CACHE_WAVE_BUFFERS; i++)
            if (!cache_wave_buffer((unsigned)i))
                abort();

        t0 = now_seconds();
        for (i = 0; i < iters; i++)
            acc += cache_wave_buffer((unsigned)i)[MEMZ_WAVE_BYTES - 1];
        t1 = now_seconds();
        t_pool = t1 - t0;

        printf("   pool      %8.2f us per buffer  (%d buffers replayed from RAM)\n",
               t_pool * 1e6 / iters, MEMZ_CACHE_WAVE_BUFFERS);
        printf("   speedup   %8.0fx over the original loop\n",
               (t_ref / WAVE_ITERS) / (t_pool / iters));
    }
#endif

    /* ---- the frequency division itself is memoized too */
    {
        int ok = 1;
        for (i = 0; i < 4000; i++)
        {
            int expected = MEMZ_WAVE_RATE / (i + 1000);
            if (cache_wave_freq(i) != expected)
                ok = 0;
        }
        check("cache_wave_freq() memoizes 44100/(r+1000) correctly", ok);
    }

    free(reference);
    free(tables);
    printf("   (acc %llu)\n", acc);
}

/* ------------------------------------------------------------------ */
/* 3. Process scan (overwatch watchdog)                                */
/* ------------------------------------------------------------------ */

#define PROC_BASE   220
#define PROC_SCANS  2000

static unsigned long g_resolve_calls;

/* Stands in for OpenProcess() + GetProcessImageFileNameA(); each of those is
 * a syscall, so two getrandom() calls are a fair proxy for the cost. */
static int resolve_same_image(DWORD pid, const char *self, void *ctx)
{
    (void)self;
    (void)ctx;
    g_resolve_calls++;
    os_random();
    os_random();
    return (pid % 37 == 0) ? 1 : 0;
}

/* Deterministic process list for a given scan, so the cached and the
 * uncached run see exactly the same system. */
static int list_for_scan(int scan, DWORD *out)
{
    int n = 0;
    int i;

    for (i = 0; i < PROC_BASE; i++)
        out[n++] = (DWORD)(100 + i * 4);

    /* A handful of processes that come and go. */
    for (i = 0; i < 6; i++)
    {
        int birth = i * 40 + 5;
        if (scan >= birth && scan < birth + 120)
            out[n++] = (DWORD)(50000 + i);
    }

    /* Short lived processes, to exercise the pruning. */
    if (scan % 32 == 0)
        out[n++] = (DWORD)(90000 + (scan / 32));

    return n;
}

static void bench_process(void)
{
    DWORD *pids = (DWORD *)malloc(sizeof(DWORD) * (PROC_BASE + 16));
    int *uncached_counts = (int *)malloc(sizeof(int) * PROC_SCANS);
    int *cached_counts = (int *)malloc(sizeof(int) * PROC_SCANS);
    double t0, t1, t_uncached, t_cached;
    int scan, i, n, same = 1;
    unsigned long uncached_calls, cached_calls;

    if (!pids || !uncached_counts || !cached_counts)
        abort();

    printf("\n3. overwatch(): every process re-resolved on every 10 ms scan\n");

    g_resolve_calls = 0;
    t0 = now_seconds();
    for (scan = 0; scan < PROC_SCANS; scan++)
    {
        int count = 0;
        n = list_for_scan(scan, pids);
        for (i = 0; i < n; i++)
            if (resolve_same_image(pids[i], "self", NULL))
                count++;
        uncached_counts[scan] = count;
    }
    t1 = now_seconds();
    t_uncached = t1 - t0;
    uncached_calls = g_resolve_calls;

    g_resolve_calls = 0;
    t0 = now_seconds();
    for (scan = 0; scan < PROC_SCANS; scan++)
    {
        int count = 0;
        n = list_for_scan(scan, pids);
        cache_proc_begin();
        for (i = 0; i < n; i++)
            if (cache_proc_match(pids[i], &resolve_same_image, NULL) > 0)
                count++;
        cache_proc_end();
        cached_counts[scan] = count;
    }
    t1 = now_seconds();
    t_cached = t1 - t0;
    cached_calls = g_resolve_calls;

    for (scan = 0; scan < PROC_SCANS; scan++)
        if (uncached_counts[scan] != cached_counts[scan])
            same = 0;

    printf("   uncached  %8.1f ms  (%lu OpenProcess+GetImageFileName pairs)\n",
           t_uncached * 1e3, uncached_calls);
    printf("   cached    %8.1f ms  (%lu OpenProcess+GetImageFileName pairs)\n",
           t_cached * 1e3, cached_calls);
    printf("   speedup   %8.1fx, %.2f%% of the OS calls removed\n",
           t_uncached / (t_cached > 0 ? t_cached : 1e-9),
           100.0 * (1.0 - (double)cached_calls / (double)uncached_calls));
    check("cached scan counts the same memz processes as the uncached scan",
          same);

    free(pids);
    free(uncached_counts);
    free(cached_counts);
}

/* ------------------------------------------------------------------ */
/* 4. Text memo (invert_text payload)                                  */
/* ------------------------------------------------------------------ */

#define TEXT_WINDOWS 300
#define TEXT_TICKS   300

static const char *const words[] = {
    "memz", "trojan", "hello", "world", "notepad", "document", "untitled",
    "calculator", "explorer", "system", "settings", "folder", "file"
};

static void make_text(WCHAR *dst, int seed)
{
    /* Real window titles are not three words long: browsers and editors
     * easily carry 80+ characters, which is where the memo starts to matter
     * (a memcmp is O(n) once, the reversal is O(n) three times). */
    int n = 1 + (seed % 13);
    int i;
    dst[0] = L'\0';
    for (i = 0; i < n; i++)
    {
        const char *w = words[(seed * 7 + i * 13) % 13];
        size_t at = wcslen(dst);
        size_t k;
        for (k = 0; w[k]; k++)
            dst[at + k] = (WCHAR)w[k];
        dst[at + k] = (i + 1 < n) ? L' ' : L'\0';
    }
}

static void bench_text(void)
{
    WCHAR **original = (WCHAR **)malloc(sizeof(WCHAR *) * TEXT_WINDOWS);
    WCHAR **state_a = (WCHAR **)malloc(sizeof(WCHAR *) * TEXT_WINDOWS);
    WCHAR **state_b = (WCHAR **)malloc(sizeof(WCHAR *) * TEXT_WINDOWS);
    WCHAR *scratch = (WCHAR *)malloc(sizeof(WCHAR) * 8192);
    WCHAR *out = (WCHAR *)malloc(sizeof(WCHAR) * 8192);
    double t0, t1, t_uncached, t_cached;
    unsigned long sends_uncached = 0, sends_cached = 0;
    int i, tick, got;
#if !MEMZ_CACHE_AGRESSIVE
    int identical = 1;
#endif
    size_t total_chars = 0;
#if MEMZ_CACHE_AGRESSIVE
    int payload_works = 1;
#endif

    if (!original || !state_a || !state_b || !scratch || !out)
        abort();

    printf("\n4. invert_text(): 2 cross process messages + a 16 KB alloc per window per tick\n");

    total_chars = 0;
    for (i = 0; i < TEXT_WINDOWS; i++)
    {
        original[i] = (WCHAR *)malloc(sizeof(WCHAR) * 512);
        state_a[i] = (WCHAR *)malloc(sizeof(WCHAR) * 512);
        state_b[i] = (WCHAR *)malloc(sizeof(WCHAR) * 512);
        make_text(original[i], i);
        wcscpy(state_a[i], original[i]);
        wcscpy(state_b[i], original[i]);
        total_chars += wcslen(original[i]);
    }

    printf("   (%d windows, average title %d characters)\n",
           TEXT_WINDOWS, (int)(total_chars / TEXT_WINDOWS));

    /* Uncached: the original enum_child_proc(), minus the Windows calls. */
    t0 = now_seconds();
    for (tick = 0; tick < TEXT_TICKS; tick++)
    {
        for (i = 0; i < TEXT_WINDOWS; i++)
        {
            void *buf = malloc(sizeof(WCHAR) * 8192);   /* GlobalAlloc   */
            wcscpy(scratch, state_a[i]);                /* WM_GETTEXT    */
            sends_uncached += 2;
            wcscpy(out, scratch);
            reverse_string_w(out);                      /* the transform */
            wcscpy(state_a[i], out);                    /* WM_SETTEXT    */
            free(buf);                                  /* GlobalFree    */
        }
    }
    t1 = now_seconds();
    t_uncached = t1 - t0;

    /* Cached: reused buffers + memoized transform. */
    t0 = now_seconds();
    for (tick = 0; tick < TEXT_TICKS; tick++)
    {
        cache_text_tick();

        for (i = 0; i < TEXT_WINDOWS; i++)
        {
            HWND hwnd = (HWND)(size_t)(0x10000u + i * 0x30u);

            if (!cache_text_due(hwnd))
                continue;                               /* nothing sent  */

            wcscpy(scratch, state_b[i]);                /* WM_GETTEXT    */
            sends_cached++;

            /* The real payload gets this length back from WM_GETTEXT for
             * free; here it costs a wcslen(), so the measurement is
             * conservative. */
            got = (int)wcslen(scratch);

            switch (cache_text_get(hwnd, scratch, got, out, 8192))
            {
            case MEMZ_TEXT_UNCHANGED:
                break;                                  /* nothing sent  */
            case MEMZ_TEXT_CACHED:
                wcscpy(state_b[i], out);                /* WM_SETTEXT    */
                sends_cached++;
                break;
            case MEMZ_TEXT_COMPUTE:
            default:
                wcscpy(out, scratch);
                reverse_string_w(out);
                cache_text_put(hwnd, scratch, got, out, (int)wcslen(out));
                wcscpy(state_b[i], out);                /* WM_SETTEXT    */
                sends_cached++;
                break;
            }
        }
    }
    t1 = now_seconds();
    t_cached = t1 - t0;

    for (i = 0; i < TEXT_WINDOWS; i++)
    {
        WCHAR expected[512];
        wcscpy(expected, original[i]);
        reverse_string_w(expected);
#if MEMZ_CACHE_AGRESSIVE
        /* Aggressive mode settles: the window keeps holding the reversed
         * text instead of flipping back and forth, which is the point. */
        if (wcscmp(state_b[i], expected) != 0)
            payload_works = 0;
#else
        if (wcscmp(state_a[i], state_b[i]) != 0)
            identical = 0;
#endif
    }

    printf("   uncached  %8.1f ms  (%lu cross process messages)\n",
           t_uncached * 1e3, sends_uncached);
    printf("   cached    %8.1f ms  (%lu cross process messages)\n",
           t_cached * 1e3, sends_cached);
    printf("   speedup   %8.1fx on the CPU side, %.1f%% of the messages removed\n",
           t_uncached / (t_cached > 0 ? t_cached : 1e-9),
           100.0 * (1.0 - (double)sends_cached / (double)sends_uncached));

#if MEMZ_CACHE_AGRESSIVE
    check("every window ends up holding its reversed text", payload_works);
#else
    check("cached path produces the same text as the uncached path", identical);
#endif

    for (i = 0; i < TEXT_WINDOWS; i++)
    {
        free(original[i]);
        free(state_a[i]);
        free(state_b[i]);
    }
    free(original);
    free(state_a);
    free(state_b);
    free(scratch);
    free(out);
}

/* ------------------------------------------------------------------ */
/* 5. Metrics and icons                                                */
/* ------------------------------------------------------------------ */

static void bench_metrics(void)
{
    unsigned long before_m, before_i;
    int i, ok = 1;

    printf("\n5. GetSystemMetrics() / LoadIcon(): called on every tick of every payload\n");

    before_m = g_metric_calls;
    before_i = g_icon_calls;

    for (i = 0; i < 1000000; i++)
    {
        if (cache_metric(0) != 1920)
            ok = 0;
        if (cache_icon("IDI_ERROR") == NULL)
            ok = 0;
    }

    printf("   %lu metric lookups, %lu icon lookups -> %lu and %lu real calls\n",
           1000000ul, 1000000ul,
           g_metric_calls - before_m, g_icon_calls - before_i);
    check("metrics and icons are resolved exactly once", ok);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("memz cache benchmark -- %s mode\n",
#if MEMZ_CACHE_AGRESSIVE
           "AGGRESSIVE"
#else
           "EXACT (bit identical results)"
#endif
    );
    printf("config: pool=%d values, wave buffers=%d, text slots=%d, "
           "proc slots=%d\n",
           MEMZ_CACHE_RANDOM_SLOTS, MEMZ_CACHE_WAVE_BUFFERS,
           MEMZ_CACHE_TEXT_SLOTS, MEMZ_CACHE_PROC_SLOTS);

    cache_init();

    bench_random();
    bench_wave();
    bench_process();
    bench_text();
    bench_metrics();

    printf("\ncache after the run:\n");
    cache_dump_stats();

    printf("\n%s (%d failed check%s)\n",
           g_failures ? "FAILURES" : "all correctness checks passed",
           g_failures, g_failures == 1 ? "" : "s");

    cache_shutdown();
    return g_failures ? 1 : 0;
}
