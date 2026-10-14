# memz

A new version of [Leurak's MEMZ Trojan](https://github.com/Leurak/MEMZ), with cleaner code and cleaner structure.

If you #define \__HARMFUL\__, this program WILL damage your computer.

I am NOT responsible for any damage caused by this program.

Differences from the original:

* New code structure to make it easier to read
* Can be compiled and linked with MinGW or Clang
* There is no GUI sandbox option
* Simpler payload delay algorithm (affects acceleration of payload cicle)
* Assembly/Images/Song not included as resources (only as bytes already embedded in the code)
* A RAM cache in front of every payload loop (`src/cache.c`): random numbers,
  waveform synthesis, window text, system metrics and the process scan are all
  computed once and reused instead of being recomputed thousands of times per
  second. See [docs/CACHING.md](docs/CACHING.md) for what it does and what it
  measured; `make -C tools/bench` runs the benchmark.
