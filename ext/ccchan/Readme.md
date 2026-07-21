# cchan

Header-only CSP / Go-style channels for C (POSIX pthreads).

Vendored for **ClassyC** (`~/src/classyc`). Value-oriented messages (`memcpy`),
buffered and unbuffered channels, blocking/non-blocking select, refcounted
lifetime, and typed helpers.

## Quick start

```c
#define CCHAN_IMPLEMENTATION   /* exactly one .c / .cpp file */
#include "cchan.h"

#include <pthread.h>
#include <stdio.h>

static void* worker(void* arg) {
    cchan_t* c = (cchan_t*)arg;
    int v = 42;
    cchan_send(c, &v);
    return 0;
}

int main(void) {
    cchan_t* c = cchan_create(0, sizeof(int)); /* unbuffered */
    pthread_t t;
    int out = 0;

    pthread_create(&t, 0, worker, c);
    cchan_recv(c, &out);
    pthread_join(t, 0);

    printf("%d\n", out);
    cchan_dispose(c);
    return 0;
}
```

Build:

```sh
gcc -O2 -Wall -Wextra -o app app.c -lpthread
```

## API summary

| Function | Role |
|---|---|
| `cchan_create(cap, msg_size)` | `cap==0` unbuffered, else ring buffer |
| `cchan_retain` / `cchan_release` / `cchan_dispose` | refcount / free |
| `cchan_close` / `cchan_is_closed` | EOF signal (close ≠ free) |
| `cchan_send` / `cchan_recv` | blocking; `1` transferred, `0` closed |
| `cchan_try_send` / `cchan_try_recv` | `1` ok, `0` would-block, `-1` closed |
| `cchan_send_budget` / `recv_budget` | spin/yield try — multi-hop hot path |
| `cchan_send_timeout` / `recv_timeout` | wall-clock bounded send/recv |
| `cchan_select` / `nb_select` / `select_timeout` | multi-channel choose |
| `cchan_size` / `cchan_capacity` / `cchan_msg_size` | introspection |
| `cchan_send_i32` / `recv_i32` / `i64` / `double` / `buf` | typed helpers |
| `cchan_sleep` | millisecond sleep |

There is **no** thread spawner — use `pthread_create`, C++ `std::thread`, or
the ClassyC runtime. Shared counters in ClassyC fiber demos use C11 atomics
(`<stdatomic.h>`); see `examples/classy-cchan-fibers.cy`.

## Semantics (Go-aligned)

- **Unbuffered** (`cap == 0`): send and receive rendezvous; data is copied
  directly between peer buffers.
- **Buffered**: send blocks only when full; receive blocks only when empty.
- **Close**: further sends fail; receives drain remaining buffered messages,
  then fail. Waiters are woken. Closing is not dispose.
- **Select**: fair random choice among currently ready cases. Blocking select
  waits without lost wakeups. Closed empty receive cases are selectable so a
  `select` can observe EOF.
- **Budget / timeout**: same try/close codes; bound waits so multi-hop graphs
  and clients cannot circular-wait. See API.md for lifecycle rules.

## Safe composition (short version)

```text
stop → close → join     // never join-then-stop
send_budget on stage outs   // never block engine on full downstream
select(work, control)       // always a stop path
recv_timeout for replies    // never park forever on a droppable terminal
```

Full patterns, cheatsheet, and ClassyC guidance: **[API.md](API.md)**.

## Testing

```sh
make test          # unit suite
make check         # unit + stress/trading/fiber --quick
make asan          # Address+UBSan unit suite
make stress        # heavy thread stress (see stress_crazy.c)
make trading       # multi-hop exchange stress (see stress_trading.c)
make fiber         # cooperative fibers + cchan (minicoro; see fiber_trading.c)
make help
```

### Fiber demo (`fiber_trading`)

Vendored [`minicoro.h`](https://github.com/edubart/minicoro) (Public Domain **or**
MIT-0) runs a tiny matching engine as **cooperative fibers on one OS thread**.
Channels use `cchan_try_*` + `mco_yield` — never blocking `cchan_send/recv`
between fibers on the same thread (that would deadlock). Prefer **buffered**
channels for fiber try/yield; unbuffered try-rendezvous needs a park hook that
cchan does not expose yet.


## License

LGPL-2.1-or-later OR MPL-2.0 (see `LICENSE.LGPLv21`, `LICENSE.MPLv2`).

Original library CshChan by Rochus Keller; header-only redesign for ClassyC.
