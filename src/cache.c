/*
 * src/cache.c -- the memz RAM cache.
 *
 * Every section below replaces a computation that the payload loops used to
 * repeat thousands of times per second. The rule of thumb used throughout:
 * if a value only depends on inputs that change rarely (or never), compute
 * it once and keep the bytes in RAM.
 *
 * The file is platform guarded so it can also be compiled on the build host
 * (see tools/bench), which is the only place where the cache is actually
 * verified for correctness.
 */

#include <memz/cache.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Wincrypt.h>
#include <Psapi.h>
#else
#include <pthread.h>
#include <sched.h>
#include <sys/random.h>
#include <sys/types.h>
#include <time.h>
#endif

/* ==================================================================== */
/* Platform primitives                                                  */
/* ==================================================================== */

#ifdef _WIN32

typedef LONG cache_atomic;

static CRITICAL_SECTION g_cs;

#define ATOMIC_INC(p)    InterlockedIncrement((LONG volatile *)(p))
#define ATOMIC_SET(p, v) ((void)InterlockedExchange((LONG volatile *)(p), (LONG)(v)))
#define ATOMIC_GET(p)    InterlockedCompareExchange((LONG volatile *)(p), 0, 0)

static int atomic_cas(volatile cache_atomic *p, cache_atomic n, cache_atomic o)
{
    return InterlockedCompareExchange((LONG volatile *)p, (LONG)n, (LONG)o) == (LONG)o;
}

static void lock_init(void) { InitializeCriticalSection(&g_cs); }
static void lock_done(void) { DeleteCriticalSection(&g_cs); }
static void lock(void)      { EnterCriticalSection(&g_cs); }
static void unlock(void)    { LeaveCriticalSection(&g_cs); }
static void yield(void)     { Sleep(0); }

/* Debug output. Nothing in the normal build calls it, but it is handy when
 * watching a payload run under a debugger and it is what tools/bench uses. */
static void cache_trace(const char *line)
{
    char buf[256];
    size_t n = strlen(line);

    if (n >= sizeof(buf) - 2)
        n = sizeof(buf) - 3;
    memcpy(buf, line, n);
    buf[n] = '\n';
    buf[n + 1] = '\0';
    OutputDebugStringA(buf);
}

#else

typedef int cache_atomic;

static pthread_mutex_t g_cs = PTHREAD_MUTEX_INITIALIZER;

#define ATOMIC_INC(p)    __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define ATOMIC_SET(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define ATOMIC_GET(p)    __atomic_load_n((p), __ATOMIC_SEQ_CST)

static int atomic_cas(volatile cache_atomic *p, cache_atomic n, cache_atomic o)
{
    return __atomic_compare_exchange_n((cache_atomic *)p, &o, n, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static void lock_init(void) {}
static void lock_done(void) {}
static void lock(void)      { pthread_mutex_lock(&g_cs); }
static void unlock(void)    { pthread_mutex_unlock(&g_cs); }
static void yield(void)     { sched_yield(); }

static void cache_trace(const char *line)
{
    if (line)
        printf("%s\n", line);
}

#endif /* _WIN32 */

/*
 * Tiny 64 bit formatter. %llu is not spelled the same way on every Windows
 * runtime memz can be built with, so the counters are converted by hand.
 */
static const char *cache_u64(unsigned long long v)
{
    static char bufs[4][24];
    static int which;
    char *out = bufs[which = (which + 1) & 3];
    char tmp[24];
    int n = 0;

    if (v == 0)
        tmp[n++] = '0';

    while (v)
    {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }

    {
        int i;
        for (i = 0; i < n; i++)
            out[i] = tmp[n - 1 - i];
        out[n] = '\0';
    }

    return out;
}

/* ==================================================================== */
/* State                                                                */
/* ==================================================================== */

#define CACHE_COLD 0
#define CACHE_BUSY 1
#define CACHE_LIVE 2

static volatile cache_atomic g_state = CACHE_COLD;
static memz_cache_stats      g_stats;

/* ---------------- random number pool ---------------- */

static unsigned int        *g_rnd_pool;      /* MEMZ_CACHE_RANDOM_SLOTS */
static volatile cache_atomic g_rnd_next;     /* next slot to hand out   */

#ifdef _WIN32
static HCRYPTPROV g_prov;
#endif

/* ---------------- GetSystemMetrics cache ---------------- */

typedef struct
{
    int index;
    int value;
    int used;
} metric_slot;

static metric_slot g_metrics[MEMZ_CACHE_METRIC_SLOTS];

/* ---------------- LoadIcon cache ---------------- */

typedef struct
{
    LPCSTR id;
    HICON  icon;
    int    used;
} icon_slot;

static icon_slot g_icons[MEMZ_CACHE_ICON_SLOTS];

/* ---------------- system sound cache ---------------- */

typedef struct
{
    LPCSTR       alias;
    const void  *data;
    size_t       size;
    int          used;
} sound_slot;

static sound_slot g_sounds[MEMZ_CACHE_SOUND_SLOTS];

/* ---------------- text transformation memo ---------------- */

typedef struct
{
    HWND         hwnd;
    int          in_len;      /* -1 = nothing memoized yet                 */
    int          out_len;
    unsigned int last_tick;   /* payload tick this window was handled on   */
    int          settled;     /* it currently holds a result we sent       */
    WCHAR       *in;          /* both point into g_text_pool               */
    WCHAR       *out;
    int          used;
} text_slot;

static text_slot g_text[MEMZ_CACHE_TEXT_SLOTS];
static WCHAR    *g_text_pool;        /* 2 * MEMZ_CACHE_TEXT_CHARS per slot */
static unsigned int g_text_tick;

/* ---------------- process image cache ---------------- */

typedef struct
{
    DWORD        pid;
    int          match;
    unsigned int generation;   /* last scan this pid was seen in */
    int          used;
} proc_slot;

static proc_slot    g_procs[MEMZ_CACHE_PROC_SLOTS];
static unsigned int g_proc_gen;
static unsigned int g_proc_scans_since_prune;
static char        *g_self_image;

/* ---------------- waveform caches ---------------- */

static int   g_freq_memo[4000];                    /* 44100 / (r + 1000)  */
static char *g_wave_tables[MEMZ_WAVE_FREQ_MAX + 1]; /* one cycle per freq */
static char *volatile g_wave_buffers[MEMZ_CACHE_WAVE_BUFFERS];

/* ==================================================================== */
/* Small helpers                                                        */
/* ==================================================================== */

static void *cache_alloc(size_t bytes)
{
    void *p = calloc(1, bytes);
    if (p)
        g_stats.bytes += bytes;
    return p;
}

static void cache_free(void *p, size_t bytes)
{
    if (!p)
        return;
    if (g_stats.bytes >= bytes)
        g_stats.bytes -= bytes;
    free(p);
}

/* ==================================================================== */
/* Init / shutdown / stats                                              */
/* ==================================================================== */

static void cache_bootstrap(void)
{
    size_t text_bytes;

    /* One big pool for the random numbers: this is what turns a syscall per
     * random() into a syscall per 65536 random()s. */
    g_rnd_pool = (unsigned int *)cache_alloc(
        (size_t)MEMZ_CACHE_RANDOM_SLOTS * sizeof(unsigned int));
    if (g_rnd_pool)
        ATOMIC_SET(&g_rnd_next, (cache_atomic)MEMZ_CACHE_RANDOM_SLOTS); /* empty */

    /* Two memoized strings (input + reversed output) per window slot. */
    text_bytes = (size_t)MEMZ_CACHE_TEXT_SLOTS * 2 * MEMZ_CACHE_TEXT_CHARS
                 * sizeof(WCHAR);
    g_text_pool = (WCHAR *)cache_alloc(text_bytes);
    if (g_text_pool)
    {
        size_t i;
        for (i = 0; i < MEMZ_CACHE_TEXT_SLOTS; i++)
        {
            g_text[i].in  = g_text_pool + (i * 2 * MEMZ_CACHE_TEXT_CHARS);
            g_text[i].out = g_text[i].in + MEMZ_CACHE_TEXT_CHARS;
        }
    }

    g_self_image = (char *)cache_alloc(1024);
}

void cache_init(void)
{
    if (ATOMIC_GET(&g_state) == CACHE_LIVE)
        return;

    if (!atomic_cas(&g_state, CACHE_BUSY, CACHE_COLD))
    {
        /* Someone else is building the cache, wait for them. */
        while (ATOMIC_GET(&g_state) != CACHE_LIVE)
            yield();
        return;
    }

    lock_init();
    cache_bootstrap();

#ifdef _WIN32
    if (g_self_image)
        GetProcessImageFileNameA(GetCurrentProcess(), g_self_image, 1024);
#else
    if (g_self_image)
        strcpy(g_self_image, "/usr/bin/memz-host-stand-in");
#endif

    ATOMIC_SET(&g_state, CACHE_LIVE);
}

void cache_shutdown(void)
{
    size_t i;

    if (ATOMIC_GET(&g_state) != CACHE_LIVE)
        return;

    lock();

    for (i = 0; i <= MEMZ_WAVE_FREQ_MAX; i++)
        cache_free(g_wave_tables[i], (size_t)MEMZ_WAVE_SEGMENT + i);
    for (i = 0; i < MEMZ_CACHE_WAVE_BUFFERS; i++)
        cache_free(g_wave_buffers[i], MEMZ_WAVE_BYTES);
    for (i = 0; i < MEMZ_CACHE_SOUND_SLOTS; i++)
        cache_free((void *)g_sounds[i].data, g_sounds[i].size);

    cache_free(g_rnd_pool,
               (size_t)MEMZ_CACHE_RANDOM_SLOTS * sizeof(unsigned int));
    cache_free(g_text_pool,
               (size_t)MEMZ_CACHE_TEXT_SLOTS * 2 * MEMZ_CACHE_TEXT_CHARS
               * sizeof(WCHAR));
    cache_free(g_self_image, 1024);

    /* Everything below degrades gracefully if these come back NULL, which is
     * exactly what has to happen once the blocks are gone. */
    g_rnd_pool   = NULL;
    g_text_pool  = NULL;
    g_self_image = NULL;
    ATOMIC_SET(&g_rnd_next, 0);

    memset(&g_metrics, 0, sizeof(g_metrics));
    memset(&g_icons, 0, sizeof(g_icons));
    memset(&g_sounds, 0, sizeof(g_sounds));
    memset(&g_text, 0, sizeof(g_text));
    memset(&g_procs, 0, sizeof(g_procs));
    memset(g_freq_memo, 0, sizeof(g_freq_memo));
    memset((void *)g_wave_tables, 0, sizeof(g_wave_tables));
    memset((void *)g_wave_buffers, 0, sizeof(g_wave_buffers));

    unlock();
    lock_done();

    ATOMIC_SET(&g_state, CACHE_COLD);
}

void cache_get_stats(memz_cache_stats *out)
{
    if (out)
        *out = g_stats;
}

static void dump_line(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cache_trace(buf);
}

void cache_dump_stats(void)
{
    memz_cache_stats s;

    cache_get_stats(&s);

#define DUMP dump_line

    DUMP("-- memz cache --------------------------------------------");
    DUMP("random   %s draws, %s refills (was %s OS calls)",
         cache_u64(s.random_draws), cache_u64(s.random_refills),
         cache_u64(s.random_draws));
    DUMP("metrics  %s hits, %s misses",
         cache_u64(s.metric_hits), cache_u64(s.metric_misses));
    DUMP("icons    %s hits, %s misses",
         cache_u64(s.icon_hits), cache_u64(s.icon_misses));
    DUMP("sounds   %s hits, %s misses",
         cache_u64(s.sound_hits), cache_u64(s.sound_misses));
    DUMP("text     %s hits, %s settled, %s misses",
         cache_u64(s.text_hits), cache_u64(s.text_settled),
         cache_u64(s.text_misses));
    DUMP("process  %s hits, %s misses",
         cache_u64(s.proc_hits), cache_u64(s.proc_misses));
    DUMP("waveform %s renders, %s replays from RAM",
         cache_u64(s.wave_renders), cache_u64(s.wave_reuses));
    DUMP("resident %.2f MB", (double)s.bytes / (1024.0 * 1024.0));
    DUMP("----------------------------------------------------------");
#undef DUMP
}

/* ==================================================================== */
/* Cached randomness                                                    */
/* ==================================================================== */

#ifndef _WIN32
/* Only used if the host has no getrandom(); the Windows build always has
 * a real CSPRNG. */
static unsigned int fallback_random(void)
{
    static unsigned int x = 0;
    if (x == 0)
        x = (unsigned int)time(NULL) * 2654435761u + 12345u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
#endif

static void rnd_fill(unsigned int *dst, size_t count)
{
#ifdef _WIN32
    if (g_prov == 0)
        if (!CryptAcquireContext(&g_prov, NULL, NULL, PROV_RSA_FULL,
                                 CRYPT_SILENT | CRYPT_VERIFYCONTEXT))
            ExitProcess(1);

    if (!CryptGenRandom(g_prov, (DWORD)(count * sizeof(unsigned int)),
                        (BYTE *)dst))
        ExitProcess(1);
#else
    unsigned char *p = (unsigned char *)dst;
    size_t left = count * sizeof(unsigned int);

    while (left)
    {
        ssize_t n = getrandom(p, left, 0);
        if (n <= 0)
        {
            size_t i;
            for (i = 0; i < count; i++)
                dst[i] = fallback_random();
            return;
        }
        p += n;
        left -= (size_t)n;
    }
#endif
}

/* Slow path: the pool ran dry. Refills it and hands out one value. */
static int cache_random_refill(void)
{
    int value;
    long i;

    lock();

    if (!g_rnd_pool)
    {
        /* No pool (out of memory at init): draw straight from the OS. */
        unsigned int one;
        rnd_fill(&one, 1);
        g_stats.random_refills++;
        g_stats.random_draws++;
        unlock();
        return (int)(one & 0x7fffffff);
    }

    if ((unsigned long)g_rnd_next >= (unsigned long)MEMZ_CACHE_RANDOM_SLOTS)
    {
        rnd_fill(g_rnd_pool, MEMZ_CACHE_RANDOM_SLOTS);
        ATOMIC_SET(&g_rnd_next, 0);
        g_stats.random_refills++;
    }

    /* Reserve a slot the same way the fast path does: threads that overshot
     * the end of the pool may still be incrementing the counter. */
    i = (long)ATOMIC_INC(&g_rnd_next) - 1;

    if (i < 0 || i >= (long)MEMZ_CACHE_RANDOM_SLOTS)
    {
        /* Pathological contention; draw one value straight from the OS. */
        unsigned int one;
        rnd_fill(&one, 1);
        unlock();
        return (int)(one & 0x7fffffff);
    }

    value = (int)(g_rnd_pool[i] & 0x7fffffff);
    g_stats.random_draws++;

    unlock();
    return value;
}

int cache_random(void)
{
    long i;

    if (g_rnd_pool)
    {
        /* Lock free fast path: reserve a slot, read it, done. */
        i = (long)ATOMIC_INC(&g_rnd_next) - 1;
        if (i >= 0 && i < (long)MEMZ_CACHE_RANDOM_SLOTS)
        {
            g_stats.random_draws++;
            return (int)(g_rnd_pool[i] & 0x7fffffff);
        }
    }

    return cache_random_refill();
}

int cache_random_below(int upper)
{
    unsigned int s;

    if (upper <= 1)
        return 0;

    s = (unsigned int)upper;

    /* Very large ranges would overflow the signed result: fall back to the
     * plain modulo the old code used. */
    if (s > 0x40000000u)
        return cache_random() % upper;

    for (;;)
    {
        unsigned int x = (unsigned int)cache_random();       /* [0, 2^31)  */
        unsigned long long m = (unsigned long long)x * s;    /* < 2^62     */
        unsigned int l = (unsigned int)(m & 0x7fffffffu);    /* m mod 2^31 */

        if (l < s)
        {
            /* Lemire's rejection threshold; a redraw happens for fewer than
             * s out of 2^31 draws. */
            unsigned int t = (0x80000000u - s) % s;
            if (l < t)
                continue;
        }

        return (int)(m >> 31);
    }
}

/* ==================================================================== */
/* Cached system metrics                                                */
/* ==================================================================== */

int cache_metric(int index)
{
    int i;

    for (i = 0; i < MEMZ_CACHE_METRIC_SLOTS; i++)
    {
        if (!g_metrics[i].used)
            break;
        if (g_metrics[i].index == index)
        {
            g_stats.metric_hits++;
            return g_metrics[i].value;
        }
    }

    lock();
    for (i = 0; i < MEMZ_CACHE_METRIC_SLOTS; i++)
        if (g_metrics[i].used && g_metrics[i].index == index)
        {
            int v = g_metrics[i].value;
            unlock();
            g_stats.metric_hits++;
            return v;
        }

    for (i = 0; i < MEMZ_CACHE_METRIC_SLOTS; i++)
        if (!g_metrics[i].used)
        {
#ifdef _WIN32
            g_metrics[i].value = GetSystemMetrics(index);
#else
            g_metrics[i].value = host_metric(index);
#endif
            g_metrics[i].index = index;
            g_metrics[i].used  = 1;
            g_stats.metric_misses++;
            unlock();
            return g_metrics[i].value;
        }

    /* Table full (16 different metrics is a lot): resolve without caching. */
    unlock();
    g_stats.metric_misses++;
#ifdef _WIN32
    return GetSystemMetrics(index);
#else
    return host_metric(index);
#endif
}

void cache_metric_invalidate(void)
{
    lock();
    memset(&g_metrics, 0, sizeof(g_metrics));
    unlock();
}

/* ==================================================================== */
/* Cached icons                                                         */
/* ==================================================================== */

HICON cache_icon(LPCSTR id)
{
    int i;

    for (i = 0; i < MEMZ_CACHE_ICON_SLOTS; i++)
    {
        if (!g_icons[i].used)
            break;
        if (g_icons[i].id == id)
        {
            g_stats.icon_hits++;
            return g_icons[i].icon;
        }
    }

    lock();
    for (i = 0; i < MEMZ_CACHE_ICON_SLOTS; i++)
        if (g_icons[i].used && g_icons[i].id == id)
        {
            HICON h = g_icons[i].icon;
            unlock();
            g_stats.icon_hits++;
            return h;
        }

    for (i = 0; i < MEMZ_CACHE_ICON_SLOTS; i++)
        if (!g_icons[i].used)
        {
#ifdef _WIN32
            g_icons[i].icon = LoadIconA(NULL, id);
#else
            g_icons[i].icon = (HICON)host_icon((const char *)id);
#endif
            g_icons[i].id   = id;
            g_icons[i].used = 1;
            g_stats.icon_misses++;
            unlock();
            return g_icons[i].icon;
        }

    unlock();
    g_stats.icon_misses++;
#ifdef _WIN32
    return LoadIconA(NULL, id);
#else
    return (HICON)host_icon((const char *)id);
#endif
}

/* ==================================================================== */
/* Cached system sounds                                                 */
/* ==================================================================== */

#ifdef _WIN32

/*
 * Resolves a sound alias ("SystemHand") to the .wav on disk through the
 * registry and reads it into RAM once. Every later play is a straight
 * PlaySoundA(pointer, SND_MEMORY), no registry walk and no file I/O.
 */
static void *sound_load(const char *alias, size_t *size)
{
    char key[256];
    char raw[MAX_PATH * 2];
    char path[MAX_PATH * 2];
    HKEY hkey;
    DWORD type = 0;
    DWORD len = sizeof(raw);
    LONG rc;
    HANDLE file;
    DWORD bytes = 0;
    void *data;

    sprintf(key, "AppEvents\\Schemes\\Apps\\.Default\\%s\\.current", alias);

    if (RegOpenKeyExA(HKEY_CURRENT_USER, key, 0, KEY_READ, &hkey)
        != ERROR_SUCCESS)
        return NULL;

    rc = RegQueryValueExA(hkey, NULL, NULL, &type, (LPBYTE)raw, &len);
    RegCloseKey(hkey);

    if (rc != ERROR_SUCCESS || raw[0] == '\0')
        return NULL;

    ExpandEnvironmentStringsA(raw, path, sizeof(path));

    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return NULL;

    bytes = GetFileSize(file, NULL);
    if (bytes == INVALID_FILE_SIZE || bytes == 0)
    {
        CloseHandle(file);
        return NULL;
    }

    data = cache_alloc(bytes);
    if (!data)
    {
        CloseHandle(file);
        return NULL;
    }

    if (!ReadFile(file, data, bytes, &bytes, NULL))
    {
        CloseHandle(file);
        cache_free(data, bytes);
        return NULL;
    }

    CloseHandle(file);
    *size = bytes;
    return data;
}

#endif /* _WIN32 */

const void *cache_sound(LPCSTR alias, size_t *size)
{
    int i;

    if (!alias)
        return NULL;

    for (i = 0; i < MEMZ_CACHE_SOUND_SLOTS; i++)
    {
        if (!g_sounds[i].used)
            break;
        if (g_sounds[i].alias == alias ||
            (g_sounds[i].alias && strcmp(g_sounds[i].alias, alias) == 0))
        {
            g_stats.sound_hits++;
            if (size)
                *size = g_sounds[i].size;
            return g_sounds[i].data;
        }
    }

#ifdef _WIN32
    lock();
    for (i = 0; i < MEMZ_CACHE_SOUND_SLOTS; i++)
        if (!g_sounds[i].used)
        {
            size_t n = 0;
            void *data = sound_load(alias, &n);
            g_sounds[i].alias = alias;
            g_sounds[i].data  = data;
            g_sounds[i].size  = data ? n : 0;
            g_sounds[i].used  = 1;
            g_stats.sound_misses++;
            unlock();
            if (size)
                *size = g_sounds[i].size;
            return g_sounds[i].data;
        }
    unlock();
#endif

    /* Not cacheable (host build, or every slot taken): let the caller fall
     * back to asking Windows to play the alias itself. */
    g_stats.sound_misses++;
    return NULL;
}

/* ==================================================================== */
/* Cached text transformation                                           */
/* ==================================================================== */

/*
 * What a window's text is, and what its reversal looks like, does not change
 * between two ticks -- unless somebody else changed the text in between, and
 * that is exactly what the comparison below detects.
 *
 * The memo deliberately does NOT hash the whole string: for the short strings
 * windows actually carry, an FNV pass costs more cycles than the reversal it
 * is supposed to avoid. Instead the length (which SendMessageTimeout() hands
 * back for free as the WM_GETTEXT result) is compared first, and only strings
 * of equal length are compared byte by byte -- which is a memcmp(), and which
 * bails out on the first difference.
 */

/*
 * Copies `len` characters and terminates. The length is always known here
 * (it comes from WM_GETTEXT or from the memo itself), so this is a memcpy()
 * and not a scan for a NUL the way strcpy() would be.
 */
static void text_copy_len(WCHAR *dst, size_t dst_chars,
                          const WCHAR *src, int len)
{
    if (dst_chars == 0)
        return;

    if (len < 0)
        len = 0;
    if ((size_t)len >= dst_chars)
        len = (int)dst_chars - 1;

    if (len > 0)
        memcpy(dst, src, (size_t)len * sizeof(WCHAR));

    dst[len] = 0;
}

/* Open addressing keyed on the window handle, multiply-shift so that even
 * sequential handles (and the real HWND values, which share their high bits)
 * spread over the whole table. */
static text_slot *text_slot_for(HWND hwnd, int create)
{
    size_t start = (size_t)((unsigned int)((size_t)hwnd * 2654435761u) >> 20)
                   & (MEMZ_CACHE_TEXT_SLOTS - 1);
    size_t i, probe;
    text_slot *victim = NULL;

    if (!g_text_pool)
        return NULL;

    for (probe = 0; probe < 8; probe++)
    {
        i = (start + probe) & (MEMZ_CACHE_TEXT_SLOTS - 1);

        if (!g_text[i].used)
        {
            /* A lookup can stop here: a stored key would have been found
             * before the first hole in its probe chain. */
            if (!create)
                return NULL;
            if (!victim)
                victim = &g_text[i];
            break;
        }

        if (g_text[i].hwnd == hwnd)
            return &g_text[i];
    }

    if (!create)
        return NULL;

    if (!victim)
        victim = &g_text[start];

    victim->hwnd      = hwnd;
    victim->in_len    = -1;
    victim->out_len   = -1;
    victim->last_tick = g_text_tick;
    victim->settled   = 0;
    victim->used      = 1;
    victim->in[0]     = 0;
    victim->out[0]    = 0;
    return victim;
}

static int text_same(const WCHAR *a, const WCHAR *b, int chars)
{
    if (chars <= 0)
        return chars == 0;

    return memcmp(a, b, (size_t)chars * sizeof(WCHAR)) == 0;
}

void cache_text_tick(void)
{
    g_text_tick++;
}

int cache_text_due(HWND hwnd)
{
#if MEMZ_CACHE_AGRESSIVE
    text_slot *s = text_slot_for(hwnd, 0);

    /* A window that already holds a result we sent, and whose probe interval
     * has not elapsed, does not even need a WM_GETTEXT: skip it entirely. */
    if (s && s->settled &&
        (unsigned int)(g_text_tick - s->last_tick) <
        (unsigned int)MEMZ_CACHE_TEXT_TICKS)
        return 0;
#else
    (void)hwnd;
#endif
    return 1;
}

/*
 * Note: the text memo is intentionally not locked. It is driven by the
 * single invert_text() payload thread -- every payload in memz owns its own
 * thread and its own windows -- and a mutex per window per tick costs more
 * than the whole reversal it is meant to save.
 */
memz_text_state cache_text_get(HWND hwnd, const WCHAR *text, int chars,
                               WCHAR *out, size_t out_chars)
{
    text_slot *s;

    if (!text || !out || out_chars == 0 || chars < 0)
        return MEMZ_TEXT_COMPUTE;

    s = text_slot_for(hwnd, 0);

    if (s)
    {
        if (chars == s->in_len && text_same(text, s->in, chars))
        {
            /* Same text as last time, so the reversal is the same too:
             * copy it out of RAM instead of recomputing it. */
            text_copy_len(out, out_chars, s->out, s->out_len);
            s->last_tick = g_text_tick;
            g_stats.text_hits++;
            return MEMZ_TEXT_CACHED;
        }

        if (chars == s->out_len && text_same(text, s->out, chars))
        {
            s->last_tick = g_text_tick;
            s->settled   = 1;

#if MEMZ_CACHE_AGRESSIVE
            /* The window still shows what we sent it: nobody has touched it,
             * so there is nothing to send back. */
            g_stats.text_settled++;
            return MEMZ_TEXT_UNCHANGED;
#else
            text_copy_len(out, out_chars, s->in, s->in_len);
            g_stats.text_hits++;
            return MEMZ_TEXT_CACHED;
#endif
        }
    }

    g_stats.text_misses++;
    return MEMZ_TEXT_COMPUTE;
}

void cache_text_put(HWND hwnd, const WCHAR *text, int chars,
                    const WCHAR *result, int result_chars)
{
    text_slot *s;

    if (!text || !result || chars < 0 || result_chars < 0)
        return;

    s = text_slot_for(hwnd, 1);
    if (!s)
        return;

    /* Very long texts are not worth memoizing: the memo would cost more RAM
     * than the reversal costs CPU, and the pool is fixed size. */
    if (chars >= MEMZ_CACHE_TEXT_CHARS || result_chars >= MEMZ_CACHE_TEXT_CHARS)
    {
        s->in_len  = -1;
        s->out_len = -1;
        s->settled = 0;
        return;
    }

    text_copy_len(s->in, MEMZ_CACHE_TEXT_CHARS, text, chars);
    text_copy_len(s->out, MEMZ_CACHE_TEXT_CHARS, result, result_chars);
    s->in_len    = chars;
    s->out_len   = result_chars;
    s->last_tick = g_text_tick;
    s->settled   = 1;
}

void cache_text_reset(void)
{
    size_t i;

    lock();
    memset(&g_text, 0, sizeof(g_text));
    for (i = 0; i < MEMZ_CACHE_TEXT_SLOTS; i++)
    {
        g_text[i].in_len  = -1;
        g_text[i].out_len = -1;
        if (g_text_pool)
        {
            g_text[i].in  = g_text_pool + (i * 2 * MEMZ_CACHE_TEXT_CHARS);
            g_text[i].out = g_text[i].in + MEMZ_CACHE_TEXT_CHARS;
        }
    }
    unlock();
}

/* ==================================================================== */
/* Cached process scan                                                  */
/* ==================================================================== */

/*
 * How long a pid stays cached after it stopped showing up in a snapshot.
 *
 * The entry has to outlive the scan it was filled in -- a snapshot visits
 * each pid exactly once, so caching only pays off across scans -- but it
 * must not live forever, or a recycled pid would inherit the answer of the
 * process that used to own it. 64 scans at 10 ms is a bit over half a
 * second, long enough that a process that is still running never falls out.
 */
#ifndef MEMZ_CACHE_PROC_MAX_AGE
#define MEMZ_CACHE_PROC_MAX_AGE 64u
#endif

const char *cache_self_image(void)
{
    cache_init();
    return g_self_image ? g_self_image : "";
}

void cache_proc_begin(void)
{
    g_proc_gen++;
}

/* Open addressing keyed on the process id. */
static proc_slot *proc_slot_for(DWORD pid, int create)
{
    size_t start = (size_t)((unsigned int)(pid * 2654435761u) >> 20)
                   & (MEMZ_CACHE_PROC_SLOTS - 1);
    size_t i, probe;
    proc_slot *victim = NULL;
    proc_slot *oldest = NULL;

    for (probe = 0; probe < 16; probe++)
    {
        i = (start + probe) & (MEMZ_CACHE_PROC_SLOTS - 1);

        if (!g_procs[i].used)
        {
            /* A lookup can stop here: a live key would have been found
             * before the first hole in its probe chain. */
            if (!create)
                return NULL;
            if (!victim)
                victim = &g_procs[i];
            continue;
        }

        if (g_procs[i].pid == pid)
            return &g_procs[i];

        if (!oldest || g_procs[i].generation < oldest->generation)
            oldest = &g_procs[i];
    }

    if (!create)
        return NULL;

    /* Recycle the emptiest slot we walked over, preferring a dead pid. */
    if (!victim)
        victim = oldest ? oldest : &g_procs[start];

    victim->pid        = pid;
    victim->match      = 0;
    victim->generation = g_proc_gen;
    victim->used       = 1;
    return victim;
}

int cache_proc_match(DWORD pid, cache_proc_resolve_fn resolve, void *ctx)
{
    proc_slot *s;
    int match;

    /* Fast path, no lock: the table is only ever touched by the scanning
     * thread, which is the only caller. */
    s = proc_slot_for(pid, 0);
    if (s && (unsigned int)(g_proc_gen - s->generation)
             <= (unsigned int)MEMZ_CACHE_PROC_MAX_AGE)
    {
        /* Still the same process: refresh the stamp so pruning keeps it. */
        s->generation = g_proc_gen;
        g_stats.proc_hits++;
        return s->match;
    }

    if (!resolve)
        return -1;

    match = resolve(pid, cache_self_image(), ctx) ? 1 : 0;

    s = proc_slot_for(pid, 1);
    if (s)
    {
        s->match      = match;
        s->generation = g_proc_gen;
    }
    g_stats.proc_misses++;

    return match;
}

void cache_proc_end(void)
{
    size_t i;
    unsigned int used = 0;
    unsigned int full_limit = MEMZ_CACHE_PROC_SLOTS
                              - (MEMZ_CACHE_PROC_SLOTS / 4);

    /* Pruning walks the whole table and the scan runs every 10 ms, so it is
     * only done when it can actually free something. */
    if (++g_proc_scans_since_prune < MEMZ_CACHE_PROC_MAX_AGE)
    {
        unsigned int n = 0;
        for (i = 0; i < MEMZ_CACHE_PROC_SLOTS; i++)
            if (g_procs[i].used)
                n++;
        if (n < full_limit)
            return;
    }

    g_proc_scans_since_prune = 0;

    for (i = 0; i < MEMZ_CACHE_PROC_SLOTS; i++)
    {
        if (!g_procs[i].used)
            continue;
        used++;
        if (g_proc_gen - g_procs[i].generation > MEMZ_CACHE_PROC_MAX_AGE)
            g_procs[i].used = 0;
    }

    /* Still packed: drop whatever is left over from older scans. */
    if (used >= full_limit)
        for (i = 0; i < MEMZ_CACHE_PROC_SLOTS; i++)
            if (g_procs[i].used && g_procs[i].generation != g_proc_gen)
                g_procs[i].used = 0;
}

/* ==================================================================== */
/* Cached waveform                                                      */
/* ==================================================================== */

int cache_wave_freq(int r)
{
    int freq;

    if (r < 0)
        r = 0;
    if (r >= (int)(sizeof(g_freq_memo) / sizeof(g_freq_memo[0])))
        r = (int)(sizeof(g_freq_memo) / sizeof(g_freq_memo[0])) - 1;

    freq = g_freq_memo[r];
    if (freq == 0)
    {
        /* Memoize the division: only 37 distinct results exist, so this
         * table is filled after the first few segments and never grows. */
        freq = MEMZ_WAVE_RATE / (r + 1000);
        if (freq < 1)
            freq = 1;
        g_freq_memo[r] = freq;
    }
    return freq;
}

/*
 * One pre-computed cycle of the buzzer ramp for a given frequency.
 *
 * The original loop evaluates
 *
 *     wavedata[i] = (char)(((i % freq) / (float)freq) * 100);
 *
 * 1 323 000 times per buffer. `i % freq` only has `freq` distinct outcomes
 * and freq is at most 44, so the whole ramp is a table of at most
 * 11025 + freq bytes. Producing a segment becomes a single memcpy() from
 * the right offset into that table -- byte for byte the same result.
 */
static const char *wave_table(int freq)
{
    char *table;
    size_t len;
    int j;

    if (freq < 1)
        freq = 1;
    if (freq > MEMZ_WAVE_FREQ_MAX)
        freq = MEMZ_WAVE_FREQ_MAX;

    table = g_wave_tables[freq];
    if (table)
        return table;

    len = (size_t)MEMZ_WAVE_SEGMENT + (size_t)freq;
    table = (char *)cache_alloc(len);
    if (!table)
        return NULL;

    for (j = 0; j < (int)len; j++)
        table[j] = (char)(((j % freq) / (float)freq) * 100.0f);

    g_wave_tables[freq] = table;
    return table;
}

void cache_wave_fill(char *dst, int samples, int freq, int phase)
{
    const char *table;

    if (!dst || samples <= 0)
        return;

    if (freq < 1)
        freq = 1;
    if (freq > MEMZ_WAVE_FREQ_MAX)
        freq = MEMZ_WAVE_FREQ_MAX;
    if (phase < 0)
        phase = 0;
    phase %= freq;

    table = wave_table(freq);
    if (table)
    {
        memcpy(dst, table + phase, (size_t)samples);
        return;
    }

    /* No memory for the table: fall back to the original computation. */
    {
        int i;
        for (i = 0; i < samples; i++)
            dst[i] = (char)((((i + phase) % freq) / (float)freq) * 100.0f);
    }
}

/* Renders a full 30 second buffer using the cycle tables: identical bytes to
 * the original generator, but 120 memcpys instead of 1.3M divisions. */
void cache_wave_render(char *dst)
{
    int seg;

    if (!dst)
        return;

    for (seg = 0; seg < MEMZ_WAVE_SECONDS * 4; seg++)
    {
        int freq  = cache_wave_freq(cache_random_below(4000));
        int phase = (seg * MEMZ_WAVE_SEGMENT) % freq;

        cache_wave_fill(dst + (seg * MEMZ_WAVE_SEGMENT),
                        MEMZ_WAVE_SEGMENT, freq, phase);
    }
}

char *cache_wave_buffer(unsigned index)
{
    unsigned i;
    char *buf;

    if (MEMZ_CACHE_WAVE_BUFFERS == 0)
        return NULL;

    i = index % MEMZ_CACHE_WAVE_BUFFERS;

    buf = g_wave_buffers[i];
    if (buf)
    {
        g_stats.wave_reuses++;
        return buf;
    }

    lock();
    buf = g_wave_buffers[i];
    if (!buf)
    {
        buf = (char *)cache_alloc(MEMZ_WAVE_BYTES);
        if (buf)
        {
            cache_wave_render(buf);
            g_stats.wave_renders++;
        }
        g_wave_buffers[i] = buf;   /* publish only once it is complete */
    }
    else
    {
        g_stats.wave_reuses++;
    }
    unlock();

    return buf;
}
