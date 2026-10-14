# Host side benchmark for the memz RAM cache.
#
#   make          build and run both modes (exact + aggressive)
#   make clean    remove build artifacts
#
# This is a build-host tool: it compiles src/cache.c on Linux/macOS with the
# Win32 shims from include/memz/host_compat.h and measures it. Nothing here
# ends up in the Windows binary.

CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
ROOT    := ../..
INC     := -I$(ROOT)/include
BUILD   := build

.PHONY: all run clean

all: syntax run

run: $(BUILD)/bench-exact $(BUILD)/bench-aggressive
	@./$(BUILD)/bench-exact
	@echo
	@echo "################################################################"
	@echo
	@./$(BUILD)/bench-aggressive

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/cache-exact.o: $(ROOT)/src/cache.c | $(BUILD)
	$(CC) -c $< -o $@ $(INC) $(CFLAGS) -DMEMZ_CACHE_AGRESSIVE=0

$(BUILD)/cache-aggressive.o: $(ROOT)/src/cache.c | $(BUILD)
	$(CC) -c $< -o $@ $(INC) $(CFLAGS) -DMEMZ_CACHE_AGRESSIVE=1

$(BUILD)/bench-exact: bench.c $(BUILD)/cache-exact.o
	$(CC) bench.c $(BUILD)/cache-exact.o -o $@ $(INC) $(CFLAGS) -DMEMZ_CACHE_AGRESSIVE=0 -lpthread

$(BUILD)/bench-aggressive: bench.c $(BUILD)/cache-aggressive.o
	$(CC) bench.c $(BUILD)/cache-aggressive.o -o $@ $(INC) $(CFLAGS) -DMEMZ_CACHE_AGRESSIVE=1 -lpthread

clean:
	rm -rf $(BUILD)

# Compile check for the Win32 only branch of src/cache.c. There is no cross
# compiler here, so the Win32 headers are stubbed out in ../winstub: this
# catches typos, missing casts and wrong signatures in code that the host
# benchmark never reaches.
WINSTUB := ../winstub

syntax: $(WINSTUB)/Windows.h $(WINSTUB)/TlHelp32.h $(WINSTUB)/Psapi.h $(WINSTUB)/Wincrypt.h
	$(CC) -fsyntax-only $(ROOT)/src/cache.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -std=c11 -Wall -Wextra
	@echo "src/cache.c: Win32 branch compiles clean"

	$(CC) -fsyntax-only $(ROOT)/src/main.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -std=c11 -Wall -Wextra
	$(CC) -fsyntax-only $(ROOT)/src/payload.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -std=c11 -Wall -Wextra
	$(CC) -fsyntax-only $(ROOT)/src/payload_function.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -std=c11 -Wall -Wextra
	$(CC) -fsyntax-only $(ROOT)/src/overwatch.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -std=c11 -Wall -Wextra
	$(CC) -fsyntax-only $(ROOT)/src/util.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -std=c11 -Wall -Wextra
	$(CC) -fsyntax-only $(ROOT)/src/payload_function.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -DMEMZ_CACHE_AGRESSIVE=0 \
	    -std=c11 -Wall -Wextra
	$(CC) -fsyntax-only $(ROOT)/src/cache.c \
	    -I$(ROOT)/include -I$(WINSTUB) -D_WIN32 -DMEMZ_CACHE_AGRESSIVE=0 \
	    -std=c11 -Wall -Wextra
	@echo "all src/*.c: Win32 branches compile clean (both cache modes)"
