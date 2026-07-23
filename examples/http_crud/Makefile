# http_crud demo — self-test, serve, and curl-based integration test.
#
#   make selftest        in-process route test (no sockets)
#   make serve           blocking server on :$(PORT)
#   make serve-fibers    fiber server (one fiber per connection) on :$(PORT)
#   make test            integration test: blocking + fibers
#   make test-fibers     integration test, fibers only
#   make test-blocking   integration test, blocking only
#
#   PORT=9090 make test-fibers

ROOT    := $(abspath ../..)
CLASSYC := $(ROOT)/build/bin/classyc
CYFLAGS := -I $(ROOT)/include -I $(ROOT)/ext/ccchan -l sqlite3

# Both C cores are always needed: main.cy references serve() and
# serve_fibers(), and the JIT resolves all externs at load time.
SRC := $(ROOT)/examples/http-serve.c \
       $(ROOT)/examples/http-serve-fibers.c \
       main.cy items.cy

PORT ?= 8080

.PHONY: all selftest serve serve-fibers test test-fibers test-blocking

all: selftest

selftest:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- test

serve:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- --port=$(PORT)

serve-fibers:
	$(CLASSYC) $(CYFLAGS) $(SRC) -eg -- fibers --port=$(PORT)

test: test-blocking test-fibers

test-fibers:
	./test.sh fibers $(PORT)

test-blocking:
	./test.sh blocking $(PORT)
