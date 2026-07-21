# cchan API reference

**cchan** is a header-only CSP / Go-style channel library for C (POSIX pthreads).

- Header: `cchan.h`
- License: LGPL-2.1-or-later OR MPL-2.0

Channels **copy message bytes** (`memcpy`); they do not take ownership of pointers
unless your message *is* a pointer. There is **no** built-in thread spawner —
create threads with `pthread_create`, C++ `std::thread`, or the ClassyC runtime.

**ClassyC counters / stop flags:** use `<stdatomic.h>` (`atomic_fetch_add`,
`atomic_load` / `store`) for shared stats — MIR emits real atomics (seq_cst).
See `examples/classy-cchan-fibers.cy` and `fiber_workers.c`. Channel internals
still use `pthread_mutex` (that is intentional, not a ClassyC limitation).

---

## Integration

```c
/* Exactly one translation unit in the program: */
#define CCHAN_IMPLEMENTATION
#include "cchan.h"

/* Every other TU: */
#include "cchan.h"
```

Link with `-lpthread` (or `pthread` on some toolchains).

C++ is supported via `extern "C"`.

---

## Type

```c
typedef struct cchan cchan_t;
```

Opaque channel handle. All API functions accept `NULL` safely where noted;
passing `NULL` as `c` generally yields a closed/no-op result.

---

## Semantic model (Go-aligned)

| Concept | Behavior |
|--------|----------|
| **Unbuffered** (`capacity == 0`) | Send and receive **rendezvous**: both sides block until a peer is present; data is copied between the two user buffers. |
| **Buffered** (`capacity > 0`) | Bounded ring of `capacity` messages. Send blocks only when full; receive blocks only when empty. |
| **Message size** | Fixed at create time. Every send/recv copies exactly `msg_size` bytes. |
| **Close** | Marks the channel closed, wakes waiters. Further **sends fail**. **Receives** first **drain** remaining buffered messages, then fail. Close does **not** free memory. |
| **Dispose / release** | Drops a reference; frees when refcount hits 0. |
| **Select** | Chooses one ready case at random among currently ready ops. Blocking select waits until any case is ready (or all are closed with nothing to deliver). |

Status codes (unless a function documents otherwise):

| Value | Meaning (blocking send/recv) |
|------:|------------------------------|
| `1` | Message transferred |
| `0` | Channel closed (or was closed while waiting); **no** transfer |

| Value | Meaning (`try_*`) |
|------:|-------------------|
| `1` | Transferred |
| `0` | Would block |
| `-1` | Closed (no transfer) |

---

## Lifecycle

### `cchan_create`

```c
cchan_t* cchan_create(unsigned short capacity, unsigned short msg_size);
```

Create a channel.

| Parameter | Meaning |
|-----------|---------|
| `capacity` | `0` → unbuffered rendezvous; `> 0` → ring capacity in messages |
| `msg_size` | Bytes per message; `0` is treated as `1` |

**Returns:** New channel with refcount `1`, or `NULL` on allocation / init failure.

**Thread safety:** The returned handle may be shared across threads without
external synchronization of the pointer itself; all operations on `c` are
internally synchronized.

---

### `cchan_retain`

```c
void cchan_retain(cchan_t* c);
```

Increment the reference count. No-op if `c == NULL`.

Use when handing the same channel to multiple independent owners (e.g. several
worker threads that each `release` when finished).

---

### `cchan_release` / `cchan_dispose`

```c
void cchan_release(cchan_t* c);
void cchan_dispose(cchan_t* c);   /* alias of cchan_release */
```

Decrement the reference count. When it reaches zero:

1. The channel is force-closed (waiters wake).
2. Mutexes/conds and heap storage are destroyed.
3. The pointer becomes invalid.

No-op if `c == NULL`. Double-release after free is undefined (do not retain a
dangling pointer).

`cchan_dispose` exists for Go / older API familiarity; prefer `cchan_release`
when thinking in refcounts.

---

### `cchan_close`

```c
int cchan_close(cchan_t* c);
```

Close the channel and wake every blocked sender, receiver, and select waiter.

| Return | Meaning |
|-------:|---------|
| `0` | This call performed the first close |
| `-1` | Already closed, or `c == NULL` |

**After close:**

- `cchan_send` / successful path of `try_send` will not transfer; send returns `0` / try returns `-1`.
- Buffered `cchan_recv` returns pending messages, then `0` with the destination buffer zeroed.
- Unbuffered in-flight rendezvous is aborted; both sides wake; receive buffers may be zeroed.
- Close is **not** dispose: you must still `cchan_release` / `cchan_dispose`.

---

### `cchan_is_closed`

```c
int cchan_is_closed(cchan_t* c);
```

| Return | Meaning |
|-------:|---------|
| `1` | Closed, or `c == NULL` |
| `0` | Open |

Snapshot only; another thread may close immediately afterward.

---

## Introspection

### `cchan_size`

```c
int cchan_size(cchan_t* c);
```

Number of messages currently buffered. Always `0` for unbuffered channels or
`NULL`. Snapshot under the channel lock.

### `cchan_capacity`

```c
int cchan_capacity(cchan_t* c);
```

Configured capacity (`0` if unbuffered or `c == NULL`). Fixed at create time.

### `cchan_msg_size`

```c
int cchan_msg_size(cchan_t* c);
```

Bytes per message (`0` if `c == NULL`). Fixed at create time.

---

## Send and receive

### `cchan_send`

```c
int cchan_send(cchan_t* c, const void* data);
```

Copy `msg_size` bytes from `data` into the channel.

- **Buffered:** Blocks while the ring is full (unless closed).
- **Unbuffered:** Blocks until a receiver rendezvous.
- **Closed:** Returns `0` immediately (or after waking from wait if closed mid-wait).

| Return | Meaning |
|-------:|---------|
| `1` | Bytes copied; message is in buffer or was delivered to a peer |
| `0` | No transfer (`c`/`data` NULL, or channel closed) |

`data` must point to at least `msg_size` readable bytes for the duration of the
call (unbuffered: until the peer finishes the copy).

---

### `cchan_recv`

```c
int cchan_recv(cchan_t* c, void* data);
```

Copy `msg_size` bytes from the channel into `data`.

- **Buffered:** Blocks while empty (unless closed).
- **Unbuffered:** Blocks until a sender rendezvous.
- **Closed + empty (or drained):** Returns `0` and **zeros** `data`.

| Return | Meaning |
|-------:|---------|
| `1` | Message written to `data` |
| `0` | No transfer; `data` zeroed if the failure was due to close/EOF |

---

### `cchan_try_send`

```c
int cchan_try_send(cchan_t* c, const void* data);
```

Non-blocking send.

| Return | Meaning |
|-------:|---------|
| `1` | Transferred |
| `0` | Would block (full buffer, or unbuffered with no waiting receiver) |
| `-1` | Closed or invalid args |

---

### `cchan_try_recv`

```c
int cchan_try_recv(cchan_t* c, void* data);
```

Non-blocking receive.

| Return | Meaning |
|-------:|---------|
| `1` | Message written to `data` |
| `0` | Would block (empty / no sender) |
| `-1` | Closed and empty (or invalid); `data` zeroed on closed empty |

---

### `cchan_send_budget` / `cchan_recv_budget`

```c
int cchan_send_budget(cchan_t* c, const void* data, unsigned spins);
int cchan_recv_budget(cchan_t* c, void* data, unsigned spins);
```

Repeat `try_send` / `try_recv` up to `spins + 1` attempts with a light yield every
64 attempts. **Preferred hot-path primitive** for multi-hop pipelines (gateway,
matcher, reply demux) so a full downstream buffer never parks the producer of an
upstream queue forever.

| Parameter | Meaning |
|-----------|---------|
| `spins` | Extra retries after the first try. `0` ≡ single `try_*`. |

| Return | Meaning |
|-------:|---------|
| `1` | Transferred |
| `0` | Budget exhausted (would still block) |
| `-1` | Closed / invalid |

**ClassyC / engine rule:** the worker that drains stage *N* must not
`cchan_send` forever into stage *N+1*. Use budget (or timeout) and apply a
policy on `0`: reject, drop, metric, or slow the upstream producer.

---

### `cchan_send_timeout` / `cchan_recv_timeout`

```c
int cchan_send_timeout(cchan_t* c, const void* data, unsigned timeout_ms);
int cchan_recv_timeout(cchan_t* c, void* data, unsigned timeout_ms);
```

Wall-clock bounded send/recv (`CLOCK_REALTIME` + `pthread_cond_timedwait`).

| Parameter | Meaning |
|-----------|---------|
| `timeout_ms` | Maximum wait. `0` ≡ `try_send` / `try_recv` (no sleep). |

| Return | Meaning |
|-------:|---------|
| `1` | Transferred |
| `0` | Timed out (no transfer); channel may still be open |
| `-1` | Closed / invalid (recv zeros `data` when closed and empty) |

Use for **request/reply clients** and any wait that must not depend on a later
shutdown path the waiter itself is blocking.

---

## Select

Select waits for **one** ready operation among a set of receives and/or sends,
then performs that operation. If several are ready, one is chosen with
`rand()` (fairness among the ready set on each call — seed with `srand` if
desired).

### Indexing

Cases are numbered in a single logical array:

```text
index 0 .. recv_count-1              → recv_chans[i] / recv_bufs[i]
index recv_count .. recv_count+send_count-1
                                     → send_chans[j] / send_bufs[j]
                                       where j = index - recv_count
```

### Readiness rules

| Case | Ready when |
|------|------------|
| Receive, buffered | Queue non-empty, **or** channel closed (EOF / zero deliver so select can progress) |
| Receive, unbuffered | A sender is waiting in rendezvous, **or** channel closed |
| Send, buffered | Not closed and queue not full |
| Send, unbuffered | A receiver is waiting in rendezvous (not closed) |

**Note:** A closed buffered/unbuffered receive case remains selectable and may
deliver a **zeroed** buffer. Application loops that `select` on long-lived
channel sets should **drop closed cases** from the next select, or use a
sentinel protocol, to avoid spinning on forever-ready EOF cases.

Empty `recv_count` and/or `send_count` are allowed; pass `NULL` arrays when the
count is `0`.

---

### `cchan_select`

```c
int cchan_select(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                 cchan_t** send_chans, void** send_bufs, unsigned send_count);
```

Blocking multi-way select.

| Return | Meaning |
|-------:|---------|
| `>= 0` | Index of the case that ran (see indexing above) |
| `-1` | No cases, or every listed channel is closed with nothing left to deliver |

On a successful receive index, `recv_bufs[i]` holds the message (or zeros if
closed-EOF was selected). On a successful send index, bytes were taken from
`send_bufs[j]`.

**Buffer lifetime:** Each `recv_bufs[i]` / `send_bufs[j]` must remain valid for
the entire call (including while blocked).

---

### `cchan_nb_select`

```c
int cchan_nb_select(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                    cchan_t** send_chans, void** send_bufs, unsigned send_count);
```

Non-blocking select. Same indexing and completion rules as `cchan_select`.

| Return | Meaning |
|-------:|---------|
| `>= 0` | Case that ran |
| `-1` | None ready, all closed, or empty case list |

Does not block the calling thread.

---

### `cchan_select_timeout`

```c
int cchan_select_timeout(cchan_t** recv_chans, void** recv_bufs, unsigned recv_count,
                         cchan_t** send_chans, void** send_bufs, unsigned send_count,
                         unsigned timeout_ms);
```

Timed multi-way select. Same indexing and readiness as `cchan_select`.
`timeout_ms == 0` is a non-blocking poll with richer closed detection than
`cchan_nb_select`.

| Return | Meaning |
|-------:|---------|
| `>= 0` | Index of the case that ran |
| `-1` | Timed out; at least one listed channel is still live/busy |
| `-2` | Empty case list, OOM, or every channel closed with nothing to deliver |

**Pattern:** worker loops should select on **work + control/stop**. On `-1`,
re-check an external stop flag; on `-2`, exit. Never select forever on work
only if an orchestrator will only stop after you join.

---

## Typed helpers

Convenience wrappers that require `cchan_msg_size(c) == sizeof(T)` (or `len`
for buffers). On size mismatch they return `0` without transferring.

```c
int cchan_send_i32(cchan_t* c, int32_t v);
int cchan_recv_i32(cchan_t* c, int32_t* out);

int cchan_send_i64(cchan_t* c, int64_t v);
int cchan_recv_i64(cchan_t* c, int64_t* out);

int cchan_send_double(cchan_t* c, double v);
int cchan_recv_double(cchan_t* c, double* out);

int cchan_send_buf(cchan_t* c, const void* buf, size_t len);
int cchan_recv_buf(cchan_t* c, void* buf, size_t len);
```

`send_buf` / `recv_buf` require `len == (size_t)cchan_msg_size(c)`.

Return codes match `cchan_send` / `cchan_recv` (`1` / `0`).

---

## Utilities

### `cchan_sleep`

```c
void cchan_sleep(unsigned milliseconds);
```

Sleep the **calling** thread for approximately `milliseconds`. Interrupted
sleeps (`EINTR`) are retried. Not a channel operation; provided for tests and
simple pacing.

---

## Concurrency and lifetime rules

1. **Share handles freely** across threads; all ops are thread-safe.
2. **Do not** use a channel after its last `release`/`dispose`.
3. Prefer **signal stop → close channels → join workers → release** (see below).
4. Unbuffered `data` pointers must remain valid until the call returns
   (peer may read/write them during rendezvous).
5. Select buffer pointers must remain valid for the duration of select.
6. Closing is idempotent for waiters; only the first close returns `0`.
7. There is no panicking “send on closed” — sends fail soft with `0`/`-1`
   (unlike Go). Detect close via return codes or `cchan_is_closed`.

---

## Lifecycle and anti-deadlock patterns

cchan is intentionally **Go-like**. Blocking `send`/`recv`/`select` are correct
CSP primitives — and they can hang forever if the surrounding **graph topology**
or **session lifecycle** is wrong. This is the same class of bug as in Go;
Go “usually just works” because goroutines are cheap, `context` is universal,
and idioms force **stop → close → wait** rather than join-before-stop.

### The invariant

> Every wait must have a release condition that does **not** require the waiter
> to already have finished.

If A waits for B, and B only unblocks after A exits (or after a join that
includes A), you have a circular wait. Channels will not invent progress.

### Orchestrator order (required)

```text
GOOD:  set_stop / cancel  →  close(channels)  →  join(workers)  →  release
BAD:   join(workers)      →  then stop/close     // classic hang
```

`cchan_close` wakes blocked senders, receivers, and select waiters. Use it as
the unblocking signal (Go: `close(ch)` + `range` / select on closed; Go
`context` cancel is the control-plane equivalent).

### Multi-hop pipelines (trading / ClassyC stages)

```text
producer → ch1 → stage A → ch2 → stage B → ch3 → consumer
```

| Pattern | Result |
|---------|--------|
| Stage A does blocking `send(ch2)` while it is the only thing draining `ch1`, and B is slow | `ch1` fills; producer blocks; whole chain freezes |
| Stage A uses `send_budget` / `try_send` and **rejects or drops** on full | Progress preserved; backpressure is explicit |
| Client blocks forever on `recv(reply)` for a message that may be dropped | Hang |
| Client uses `recv_timeout` / residual abort on stop | Session completes |

**Engine rule:** the hot path that *creates* capacity (matcher, forwarder) must
not park forever on its own output. Reply/control planes need either:

- a separate drain thread,
- a large enough buffer with monitoring, or
- budgeted send + documented drop/reject policy.

### Select always includes control

```c
/* worker */
for (;;) {
    cchan_t* recvs[2] = { work, control };
    void* bufs[2] = { &msg, &ctrl };
    int idx = cchan_select_timeout(recvs, bufs, 2, NULL, NULL, 0, 100);
    if (idx == -2) break;          /* all closed */
    if (idx < 0) {                 /* timeout: re-check external stop */
        if (atomic_load(stop)) break;
        continue;
    }
    if (idx == 1 && ctrl.type == SHUTDOWN) break;
    process(&msg);
}
```

Go equivalent: `select { case v := <-work: ...; case <-ctx.Done(): return }`.

### Request / reply (order-style) protocols

Channels transfer bytes; they do **not** track in-flight request IDs. If you
need “every order gets a terminal”:

1. Track pending IDs outside the channel.
2. Use `recv_timeout` (or select with stop) on the reply path.
3. On session teardown, force `SESSION_ABORT` (or equivalent) for residual LIVE.
4. Never `join` clients that are still waiting for terminals you only emit after join.

### What “never hangs” can mean

| Promise | Realistic? |
|---------|------------|
| Raw `cchan_send` never blocks forever under any app bug | **No** |
| Ops using only timeout/budget never park without a bound | **Yes** |
| Session stop always unblocks if close is used | **Yes** |
| No message ever dropped | **No** (unless capacity/wait is unbounded) |

ClassyC codegen should default to **budget/timeout + control select + stop→close→join**, not bare blocking send/recv on multi-hop graphs.

### Cheatsheet

```text
GOOD
  try_send / send_budget / send_timeout on multi-hop outs
  select(work, control) or select_timeout + stop flag
  stop → close → join
  drop/reject/metric when out is full (explicit policy)

BAD
  send forever into a small full channel mid-pipeline
  recv forever for a reply that may be dropped
  join workers, then send shutdown that those workers need
  engine blocks on reply path while also being the ingress drain
```

See `stress_trading.c` for a multi-gateway / matcher exercise of these rules,
and `stress_crazy.c` for raw concurrency / close races.

---

## Minimal examples

### Buffered pipeline stage

```c
cchan_t* in  = cchan_create(32, sizeof(int));
cchan_t* out = cchan_create(32, sizeof(int));
/* worker: */
int x;
while (cchan_recv(in, &x)) {
    x = x * 2;
    if (!cchan_send(out, &x)) break;
}
cchan_close(out);
```

### Unbuffered rendezvous

```c
cchan_t* c = cchan_create(0, sizeof(int));
/* thread A */ int v = 42; cchan_send(c, &v);
/* thread B */ int o;      cchan_recv(c, &o);  /* o == 42 */
```

### Select fan-in (drop closed channels)

```c
cchan_t* live[N]; /* populate */
int nlive = N;
while (nlive > 0) {
    int vals[N];
    void* bufs[N];
    cchan_t* chans[N];
    int i;
    for (i = 0; i < nlive; i++) {
        chans[i] = live[i];
        bufs[i] = &vals[i];
    }
    int idx = cchan_select(chans, bufs, (unsigned)nlive, NULL, NULL, 0);
    if (idx < 0) break;
    if (cchan_is_closed(live[idx]) && cchan_size(live[idx]) == 0
        && /* optional: protocol says 0 is EOF only when closed */) {
        live[idx] = live[--nlive];
        continue;
    }
    handle(vals[idx]);
}
```

### Shared ownership

```c
cchan_t* c = cchan_create(8, sizeof(int));
cchan_retain(c);           /* for worker */
/* main and worker each release when done */
cchan_release(c);
```

### Budgeted forward (anti circular-wait)

```c
int forward(cchan_t* out, const msg_t* m)
{
    int rc = cchan_send_budget(out, m, 256);
    if (rc == 1) return 1;
    if (rc < 0)  return 0;   /* closed */
    /* full: caller rejects / drops — do NOT block the engine */
    return 0;
}
```

### Timed client receive

```c
msg_t reply;
int rc = cchan_recv_timeout(to_client, &reply, 100);
if (rc == 1)      handle(&reply);
else if (rc == 0) /* timeout: retry, abort in-flight, or stop */ ;
else              /* closed */ ;
```

### Stop → close → join

```c
atomic_store(stop, 1);
cchan_close(work);     /* wakes select/recv */
cchan_close(control);
pthread_join(worker, 0);
cchan_release(work);
cchan_release(control);
```

---


---

## Build and test

```sh
make              # build unit test + stress
make test         # run unit suite
make check        # unit + stress --quick
make asan         # ASan/UBSan unit suite
make stress       # full stress harness
make stress-quick
make lib          # optional libcchan.a
```

See `Readme.md` for overview, `stress_crazy.c` for heavy thread stress, and
`stress_trading.c` for multi-hop lifecycle / anti-deadlock exercise.
