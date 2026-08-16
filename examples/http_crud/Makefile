# http_crud demo — self-test, serve, and curl-based integration test.
#
#   make selftest        in-process route test (no sockets)
#   make serve           blocking server on :$(PORT)
#   make serve-fibers    fiber server (one fiber per connection) on :$(PORT)
#   make serve-workers   multi-OS-thread workers + Chan fan-out on :$(PORT)
#   make test            integration: blocking + fibers + workers
#   make test-fibers     integration test, fibers only
#   make test-blocking   integration test, blocking only
#   make test-workers    integration test, multi-thread workers only
#
#   make aot             native binary  →  ./http_crud
#   make aot-test        selftest via the AOT binary
#   make aot-serve       run AOT binary (blocking) on :$(PORT)
#   make aot-serve-fibers / aot-serve-workers
#   make clean           remove AOT binary + intermediates
#
#   PORT=9090 WORKERS=8 make test-workers

ROOT    := $(abspath ../..)
CLASSYC := $(ROOT)/build/bin/classyc
B2OBJ   := $(ROOT)/build/bin/b2obj
AOT_SH  := $(ROOT)/classyc-aot.sh
# -ffibers: go / Chan / cy_sched (workers mode).  Always on so one binary
# covers blocking + fibers + workers without a rebuild.
#
# http-serve-fibers.c is NOT used (see main.cy's "fibers" mode comment): it
# #includes ext/ccchan/minicoro.h directly, which classyc's parser can't
# accept (a hand-written top-level __asm__ block for the coroutine context
# switch) -- and even a parser fix wouldn't help, since MIR has no mechanism
# to emit raw inline assembly at all. http-serve-cchan.c (plain pthreads,
# no coroutines) replaces it.
CYFLAGS := -I $(ROOT)/include -I $(ROOT)/ext/ccchan -ffibers -w -l sqlite3

# Server cores + app.  JIT resolves all externs at load time, so include
# every mode even when only one is selected at runtime.
SRC := $(ROOT)/examples/http-serve.c \
       $(ROOT)/examples/http-serve-cchan.c \
       $(ROOT)/examples/http-serve-workers.cy \
       main.cy items.cy

# Paths relative to ROOT for classyc-aot.sh (it resolves tools from repo root).
AOT_SRC := examples/http-serve.c \
           examples/http-serve-cchan.c \
           examples/http-serve-workers.cy \
           examples/http_crud/main.cy \
           examples/http_crud/items.cy

BIN     := http_crud
PORT    ?= 8080
WORKERS ?= 4

.PHONY: all selftest serve serve-fibers serve-workers \
        test test-fibers test-blocking test-workers \
        aot aot-test aot-serve aot-serve-fibers aot-serve-workers clean

all: selftest

selftest:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- test

serve:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- --port=$(PORT)

serve-fibers:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- fibers --port=$(PORT)

serve-workers:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- workers --workers=$(WORKERS) --port=$(PORT)

test: test-blocking test-fibers test-workers

test-fibers:
	./test.sh fibers $(PORT)

test-blocking:
	./test.sh blocking $(PORT)

test-workers:
	./test.sh workers $(PORT) $(WORKERS)

# ── AOT (native binary via classyc-aot.sh) ─────────────────────────────
# Drive from $(ROOT) so tools/runtime resolve correctly.  Override C2M/B2OBJ
# to the *build* tree (repo bin/ can lag behind).  -ffibers → CHANFIBERS in
# mir-aot-runtime, which links a full cchan_t implementation of its own;
# -D CY_FIBERS_PROVIDE_CCHAN tells http-serve-cchan.c to skip its own copy
# so the two don't collide as duplicate symbols in the final native link
# (harmless under JIT above, a hard error under AOT -- see that file's
# comment).  GNU ld provides __start_cyreg_routes / __stop_cyreg_routes for
# the cyreg_routes section.

aot: $(BIN)

$(BIN): $(SRC) $(AOT_SH) $(CLASSYC) $(B2OBJ)
	cd $(ROOT) && C2M=$(CLASSYC) B2OBJ=$(B2OBJ) $(AOT_SH) \
	    -I include -I ext/ccchan -ffibers -l sqlite3 -D CY_FIBERS_PROVIDE_CCHAN \
	    $(AOT_SRC) -o examples/http_crud/$(BIN)

aot-test: $(BIN)
	./$(BIN) test

aot-serve: $(BIN)
	./$(BIN) --port=$(PORT)

aot-serve-fibers: $(BIN)
	./$(BIN) fibers --port=$(PORT)

aot-serve-workers: $(BIN)
	./$(BIN) workers --workers=$(WORKERS) --port=$(PORT)

clean:
	rm -f $(BIN) *.o *.bmir
