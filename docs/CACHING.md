# The memz RAM cache

Every payload in memz is an infinite loop, which makes it a bad place to
recompute things. This document describes what `src/cache.c` keeps in RAM,
what each cache replaces, what it costs, and what it measured.

The short version: the payload loops now do almost no computation and almost
no system calls, because none of their inputs change often enough to justify
either.

```
                      before                          after
random()       1 CryptGenRandom() syscall     1 syscall per 65536 draws
               per number
buzzer()       1 323 000 float divisions      120 memcpys
               per 30 s buffer, forever       (or nothing at all)
invert_text()  2 cross process messages       ~0.5 messages per window
               + 16 KB alloc/free per         per tick, no allocation
               window per tick
overwatch()    OpenProcess() +               once per process that has
               GetProcessImageFileNameA()    never been seen before
               for every process, every
               10 ms
draw_icons()   GetSystemMetrics() +           once, ever
               LoadIcon() per tick
```

---

## 1. Random number pool

`random()` was one `CryptGenRandom()` **syscall** per number, and the payloads
ask for a few hundred numbers per second (the buzzer alone wants 120 per audio
buffer).

`cache_random()` keeps `MEMZ_CACHE_RANDOM_SLOTS` (65536) numbers in RAM and
refills the whole pool with a single `CryptGenRandom()` call. The values are
still cryptographically random and cover the same range as before.

`cache_random_below(upper)` additionally replaces `random() % upper` - an
integer division - with a multiply and a shift (Lemire's method, with a
rejection step that fires for fewer than `upper` out of 2^31 draws, so it stays
unbiased).

| | |
|---|---|
| RAM | 256 KB |
| Measured | 2 000 000 draws: **15.8x** faster (319.6 → 20.2 ns/draw), 2 000 000 syscalls → 31 |

## 2. Waveform caches (the buzzer)

The original inner loop:

```c
int freq = 0;
for (int i = 0; i < bufsize; i++)
{
    if (i % (44100 / 4) == 0)
        freq = 44100 / ((random() % 4000) + 1000);

    wavedata[i] = (char)(((i % freq) / ((float)freq)) * 100);
}
```

`i % freq` has only `freq` distinct outcomes and `freq` is at most 44, so the
whole ramp for a frequency is a table of at most `11025 + freq` bytes. There
are 37 possible frequencies, so **the entire waveform the buzzer can ever
produce is about 450 KB of tables**, built once.

* `cache_wave_fill()` turns each of the 120 segments of a buffer into a single
  `memcpy()` out of the right table at the right offset. This is **byte for
  byte** what the loop above produces - the benchmark verifies it with
  `memcmp()`.
* `cache_wave_freq()` memoizes the `44100 / (r + 1000)` division (a 16 KB
  table, 37 distinct results).
* `cache_wave_buffer()` (aggressive mode) goes one step further: whole 30
  second buffers are rendered once and replayed from RAM forever after.

| | |
|---|---|
| RAM | ~0.45 MB of cycle tables; + 1.32 MB per pre-rendered buffer (16 by default = 21.2 MB) |
| Measured | **78x** faster per buffer from the tables alone; the pre-rendered pool costs ~0 µs per buffer (16 renders, then pure replay) |

## 3. Text transformation memo (invert_text)

`invert_text()` used to do this to every window on the desktop, every two
seconds, forever:

* `GlobalAlloc()` 16 KB, `SendMessageTimeout(WM_GETTEXT)`, `reverse_string_w()`,
  `SendMessageTimeout(WM_SETTEXT)`, `GlobalFree()`.

Now:

* One pair of static buffers is reused for every window - no allocation.
* The reversal is memoized per window. The memo compares by **length first**
  (`SendMessageTimeout` returns the character count for free) and only then
  runs a `memcmp()`. Hashing the whole string was tried first and rejected: for
  the short strings windows actually carry, FNV costs more cycles than the
  reversal it was meant to avoid.
* Aggressive mode: a window that still holds the result we last sent it is
  **not even asked again** until its probe interval is up, and gets no
  `WM_SETTEXT` either.

The memo is deliberately not locked: it is driven by the single `invert_text()`
payload thread, and a mutex per window per tick costs more than the whole
reversal.

| | |
|---|---|
| RAM | 1 MB (512 windows x 2 strings x 512 characters) + 2 KB for the cached child window list |
| Measured | exact mode: **1.4x** and identical results; aggressive mode: **3.2x** on the CPU side and **74.8% fewer** cross process messages (180 000 → 45 300 over 300 windows x 300 ticks) |

## 4. Process scan cache (overwatch watchdog)

The watchdog wakes up every 10 ms and asks "how many copies of me are
running?". It used to answer that by calling `OpenProcess()` and
`GetProcessImageFileNameA()` for **every process in the system**, plus a
`LocalAlloc()`/`LocalFree()` pair for each one, ten times a second.

Whether a pid runs our own image does not change while that process lives, so
`cache_proc_match()` remembers the answer. The two syscalls now only happen for
pids that have never been seen before - a handful per minute instead of ~2 000
per second on a busy machine.

Entries are dropped once a pid has been missing for
`MEMZ_CACHE_PROC_MAX_AGE` scans (64, a bit over half a second), so a recycled
pid cannot inherit the answer of the process that used to own it. The 512 byte
path buffer is allocated once instead of once per process per scan.

| | |
|---|---|
| RAM | 32 KB (2048 slots) |
| Measured | 2 000 scans over ~220 processes: **72x** faster, **99.93%** of the OS calls removed (440 783 → 289), and the cached scan counts exactly the same processes as the uncached one |

## 5. Metrics, icons and sounds

* `cache_metric()` - `GetSystemMetrics()` results do not change while memz
  runs. Called twice per message box, twice per `draw_icons()` tick.
* `cache_icon()` - `LoadIcon()` returns shared system icon handles; asking for
  `IDI_ERROR` and `IDI_WARNING` on every tick was pointless.
* `cache_sound()` - resolves a system sound alias (`"SystemHand"`) through the
  registry once, reads the `.wav` into RAM, and plays it with `SND_MEMORY`.
  Every later play skips the registry lookup and the file read.

| | |
|---|---|
| RAM | a few KB, plus the bytes of the cached sounds (~150 KB for the three system sounds) |
| Measured | 1 000 000 lookups → 1 real call each |

---

## Total footprint

| mode | resident |
|---|---|
| `MEMZ_CACHE_AGRESSIVE=0` | 2.55 MB |
| `MEMZ_CACHE_AGRESSIVE=1` | 22.83 MB (16 pre-rendered 30 s audio buffers) |

## Knobs

Every one of these can be overridden on the compiler command line, e.g.
`-DMEMZ_CACHE_WAVE_BUFFERS=4`.

| knob | default | meaning |
|---|---|---|
| `MEMZ_CACHE_AGRESSIVE` | 1 | see below |
| `MEMZ_CACHE_RANDOM_SLOTS` | 65536 | numbers per `CryptGenRandom()` call (x4 bytes) |
| `MEMZ_CACHE_WAVE_BUFFERS` | 16 | pre-rendered 30 s buffers (x1.32 MB) |
| `MEMZ_CACHE_TEXT_SLOTS` | 512 | windows kept in the text memo |
| `MEMZ_CACHE_TEXT_CHARS` | 512 | longest title that gets memoized |
| `MEMZ_CACHE_TEXT_TICKS` | 2 (1 in exact mode) | ticks a settled window is left alone |
| `MEMZ_CACHE_CHILD_SLOTS` | 256 | cached child window handles |
| `MEMZ_CACHE_CHILD_REFRESH` | 4 | ticks a cached child list stays valid |
| `MEMZ_CACHE_PROC_SLOTS` | 2048 | pids in the process table |
| `MEMZ_CACHE_PROC_MAX_AGE` | 64 | scans a dead pid is remembered for |

Dropping `MEMZ_CACHE_WAVE_BUFFERS` to 4 saves 16 MB and still gives the buzzer
two minutes of variety before it repeats.

## Aggressive vs exact mode

`MEMZ_CACHE_AGRESSIVE=0` only memoizes things whose result is **bit identical**
to the old code: the audio still comes out of the cycle tables (78x cheaper,
same bytes), text still flips back and forth on every tick, every window is
still visited every tick, randomness is still cryptographic.

`MEMZ_CACHE_AGRESSIVE=1` (the default) additionally trades fidelity for CPU:

* whole audio buffers are pre-rendered and replayed instead of being
  synthesised - the buzzer sounds the same, but the byte stream is a rotation
  of 16 buffers instead of a fresh random one;
* a window whose title is already reversed is left alone, so titles stop
  flipping back and forth and stay reversed;
* the child window list is enumerated every 4 ticks instead of every tick, so
  a window created in between is picked up within 8 seconds.

None of these affect the destructive parts of the payloads.

---

## Verifying it

```
make -C tools/bench
```

`src/cache.c` is platform guarded, so the same code that ships in the Windows
binary is compiled on the build host, measured, and checked for correctness:

* two binaries are built (aggressive and exact) and both are run;
* the cycle tables are compared byte for byte against the original generator;
* the cached process scan must count the same processes as an uncached scan;
* in exact mode, every window must end up with exactly the text the uncached
  code would have produced;
* in aggressive mode, every window must end up holding its reversed text;
* `cache_random_below()` is checked for range and uniformity over 1M draws.

The same `make` invocation also runs `syntax`, which compiles every `src/*.c`
against the Win32 header stubs in `tools/winstub` (there is no cross compiler
here, but the Win32 branches still get type checked).

Sample output:

```
1. random():  uncached 319.6 ns/draw  ->  cached 20.2 ns/draw      (15.8x)
2. buzzer():  uncached   3.3 ms/buf   ->  tables  0.04 ms/buf      (78x, identical bytes)
              pre-rendered pool: ~0 us/buffer
3. overwatch(): 440 783 -> 289 OS calls                            (99.93% removed, 72x)
4. invert_text(): 180 000 -> 45 300 messages                       (74.8% removed, 3.2x)
5. metrics/icons: 1 000 000 lookups -> 1 real call
```
