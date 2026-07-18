# Makefile for examples/beyond-demo
#
# Requires the ClassyC compiler to be built:
#   cmake -B build -S . && cmake --build build
# Then:
#   make -C examples/beyond-demo

CLASSYC ?= ../../bin/classyc
AOT_SH  ?= ./classyc-aot.sh

.PHONY: all core server test server-test stress stress-customers aot aot-test clean

all: core server

core:
	$(CLASSYC) -I ../../include classyc-db-core.cy -eg

server:
	$(CLASSYC) -I ../../include -I ../../ext/ccchan ../../examples/http-serve.c ../../examples/http-serve-fibers.c classyc-db-server.cy -eg

test: core server-test

server-test:
	$(CLASSYC) -I ../../include -I ../../ext/ccchan ../../examples/http-serve.c ../../examples/http-serve-fibers.c classyc-db-server.cy -eg test

stress:
	python3 stress_client.py --start-server --seed 1000 --ops 2000 --concurrency 50

stress-customers:
	python3 stress_customers.py --customers 1000000 --concurrency 50

# AOT: classyc-aot.sh resolves its tools + runtime relative to the repo root,
# so drive it from there and drop the binary back in this directory.
aot:
	cd ../.. && $(AOT_SH) -I include -I ext/ccchan \
	    examples/http-serve.c examples/http-serve-fibers.c \
	    examples/beyond-demo/classyc-db-server.cy \
	    -o examples/beyond-demo/classyc-db

aot-test: aot
	./classyc-db test

clean:
	rm -f classyc-db *.o *.bmir
