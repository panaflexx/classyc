# Makefile for examples/beyond-demo
#
# Requires the ClassyC compiler to be built:
#   cmake -B build -S . && cmake --build build
# Then:
#   make -C examples/beyond-demo

CLASSYC ?= ../../bin/classyc
AOT_SH  ?= ./classyc-aot.sh

# classyc-db-server.cy's main() picks its serve_* implementation off argv, so
# every build links all three server cores together (http-serve.c,
# http-serve-workers.cy for -ffibers' go/Chan<T> pool, http-serve-cchan.c for
# the plain-pthread alternative) -- classyc resolves symbols for the whole
# program at JIT-link time regardless of which argv branch actually runs, so
# an unlinked serve_* would fail to load even on a path never taken.
SERVER_SRCS = ../../examples/http-serve.c ../../examples/http-serve-workers.cy \
              ../../examples/http-serve-cchan.c
# NOTE: no -D CY_FIBERS_PROVIDE_CCHAN here on purpose. Under JIT, -ffibers'
# cchan_t usage is entirely internal to the precompiled driver binary (never
# exposed as an importable symbol), so http-serve-cchan.c must still bring
# its own CCHAN_IMPLEMENTATION for raw cchan_* calls to resolve at all --
# unlike the AOT link below, there's no real-symbol collision to avoid here.
SERVER_FLAGS = -I ../../include -I ../../ext/ccchan -ffibers -w

.PHONY: all core server test server-test server-cchan server-test-cchan \
        stress stress-customers aot aot-test clean

all: core server

core:
	$(CLASSYC) -I ../../include classyc-db-core.cy -eg

# Default server: go/Chan<T> fiber worker pool (-ffibers, http-serve-workers.cy).
server:
	$(CLASSYC) $(SERVER_FLAGS) $(SERVER_SRCS) classyc-db-server.cy -eg

test: core server-test

server-test:
	$(CLASSYC) $(SERVER_FLAGS) $(SERVER_SRCS) classyc-db-server.cy -eg test

# Alternate server: plain pthread worker pool over ext/ccchan's cchan_t
# directly (no fibers involved at runtime, just a different argv branch of
# the same build).
server-cchan:
	$(CLASSYC) $(SERVER_FLAGS) $(SERVER_SRCS) classyc-db-server.cy -eg cchan

server-test-cchan: server-test

stress:
	python3 stress_client.py --start-server --seed 1000 --ops 2000 --concurrency 50

stress-customers:
	python3 stress_customers.py --customers 1000000 --concurrency 50

# AOT: classyc-aot.sh resolves its tools + runtime relative to the repo root,
# so drive it from there and drop the binary back in this directory. (Same
# flags as SERVER_FLAGS above, just repo-root-relative instead of
# this-dir-relative since `cd ../..` changes where "." is.)
aot:
	cd ../.. && $(AOT_SH) -I include -I ext/ccchan -ffibers -w -D CY_FIBERS_PROVIDE_CCHAN \
	    examples/http-serve.c examples/http-serve-workers.cy \
	    examples/http-serve-cchan.c \
	    examples/beyond-demo/classyc-db-server.cy \
	    -o examples/beyond-demo/classyc-db

aot-test: aot
	./classyc-db test

clean:
	rm -f classyc-db *.o *.bmir
