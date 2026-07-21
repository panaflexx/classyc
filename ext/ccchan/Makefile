# cchan — Makefile
#
# Targets:
#   make / make all      build unit test + stress harnesses
#   make test            run unit suite
#   make stress          thread stress (default scale)
#   make stress-quick    shorter thread stress
#   make stress-insane   heavy thread stress
#   make trading         order-matching / exchange stress
#   make trading-quick   shorter trading stress
#   make trading-insane  heavy trading stress
#   make fiber           cooperative fiber + cchan trading demo
#   make fiber-quick     shorter fiber demo
#   make fiber-insane    512 fibers trading stress (1 OS thread)
#   make fiber-workers   500 fibers on N OS threads + cchan
#   make asan / tsan     unit suite under sanitizers
#   make check           test + stress-quick + trading-quick + fiber-quick
#   make lib             optional static lib 
#   make clean
#
# Override examples:
#   make CC=clang CFLAGS='-O3 -march=native'
#   make trading TRADING_FLAGS=-v

CC       ?= gcc
CFLAGS   ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CPPFLAGS ?= -I.
LDFLAGS  ?=
LDLIBS   ?= -lpthread

ASAN_FLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN_FLAGS = -O1 -g -fsanitize=thread -fno-omit-frame-pointer

.PHONY: all test stress stress-quick stress-insane \
        trading trading-quick trading-insane \
        fiber fiber-quick fiber-insane fiber-workers \
        asan tsan check lib clean help

all: test_bin stress_crazy stress_trading fiber_trading fiber_workers

help:
	@echo "cchan build targets:"
	@echo "  all             build unit test + stress binaries (default)"
	@echo "  test            build and run unit suite"
	@echo "  stress          thread stress harness (default scale)"
	@echo "  stress-quick    thread stress --quick"
	@echo "  stress-insane   thread stress --insane"
	@echo "  trading         order-matching / exchange stress"
	@echo "  trading-quick   trading --quick"
	@echo "  trading-insane  trading --insane"
	@echo "  fiber           cooperative fiber + cchan trading demo"
	@echo "  fiber-quick     fiber demo --quick"
	@echo "  fiber-insane    512 fibers trading stress (1 OS thread)"
	@echo "  fiber-workers   500 fibers / 8 workers, 60s orders + 1s stats"
	@echo "  asan            unit suite under Address+UBSan"
	@echo "  tsan            unit suite under ThreadSanitizer"
	@echo "  check           test + stress-quick + trading-quick + fiber-quick"
	@echo "  lib             build libcchan.a"
	@echo "  clean           remove binaries/objects"

# --- unit suite (header-only: test.c defines CCHAN_IMPLEMENTATION) -----------

test_bin: test.c cchan.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ test.c $(LDFLAGS) $(LDLIBS)

test: test_bin
	./test_bin

# --- thread stress harness ---------------------------------------------------

stress_crazy: stress_crazy.c cchan.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ stress_crazy.c $(LDFLAGS) $(LDLIBS)

stress: stress_crazy
	./stress_crazy $(STRESS_FLAGS)

stress-quick: stress_crazy
	./stress_crazy --quick

stress-insane: stress_crazy
	./stress_crazy --insane

# --- trading / order-matching stress -----------------------------------------

stress_trading: stress_trading.c cchan.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ stress_trading.c $(LDFLAGS) $(LDLIBS)

trading: stress_trading
	./stress_trading $(TRADING_FLAGS)

trading-quick: stress_trading
	./stress_trading --quick

trading-insane: stress_trading
	./stress_trading --insane

# --- fiber + cchan demo (minicoro cooperative scheduler) ---------------------

fiber_trading: fiber_trading.c cchan.h minicoro.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fiber_trading.c $(LDFLAGS) $(LDLIBS)

fiber: fiber_trading
	./fiber_trading $(FIBER_FLAGS)

fiber-quick: fiber_trading
	./fiber_trading --quick

fiber-insane: fiber_trading
	./fiber_trading --insane

fiber_workers: fiber_workers.c cchan.h minicoro.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fiber_workers.c $(LDFLAGS) $(LDLIBS)

fiber-workers: fiber_workers
	./fiber_workers $(FIBER_FLAGS)

# --- sanitizers --------------------------------------------------------------

test_asan: test.c cchan.h
	$(CC) $(CPPFLAGS) $(ASAN_FLAGS) -o $@ test.c $(LDFLAGS) $(LDLIBS)

asan: test_asan
	./test_asan

test_tsan: test.c cchan.h
	$(CC) $(CPPFLAGS) $(TSAN_FLAGS) -o $@ test.c $(LDFLAGS) $(LDLIBS)

tsan: test_tsan
	./test_tsan

# --- convenience -------------------------------------------------------------

check: test stress-quick trading-quick fiber-quick

lib: libcchan.a

clean:
	rm -f test_bin test_asan test_tsan stress_crazy stress_trading \
	      fiber_trading fiber_workers test libcchan.a *.o
