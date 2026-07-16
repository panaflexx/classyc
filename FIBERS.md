# FIBERS.md — TLS, Fibers & Channels for ClassyC

> **Status:** design checklist (not implemented).  
> **Related:** `ext/ccchan/`, `examples/classy-cchan.cy`, `examples/classy-cchan-fibers.cy`, `classyc-aot.sh`, `src/b2obj.c`, `src/mir-aot-runtime.c`

---

## Context

ClassyC already has working **library-level** CSP pieces under the JIT:

| Piece | Today |
|--------|--------|
| Channels | `ext/ccchan/cchan.h` (buffered/unbuffered, select, timeouts) |
| Fibers (C) | `minicoro.h` + try/yield pattern (`examples/classy-cchan-fibers.cy`) |
| Threads | `pthread_*` (resolve from host under JIT; AOT links `-lpthread`) |
| `_Thread_local` | **Parsed only** — warning: *"Thread local is not implemented"* → shared static |
| Language | No `go` / `await` / `chan T` yet |

**Goal:** make the **compiler** support real TLS, a cooperative fiber runtime, and Go-style `go subroutine(...)` + channels, for both:

```text
JIT:  ./bin/classyc -I include … file.cy -eg
AOT:  ./classyc-aot.sh -I include … file.cy -o prog   # classyc -c → b2obj → gcc
```

**Design principle:** yields are **compiler-mediated** (`await`, loop back-edges), not invisible `mco_yield` from deep C. That keeps String arenas, `defer`/`owned`, and exceptions well-defined.

```text
  User:  go f();  x = await ch.recv();  for (…) { … }
              │              │                    │
  Lower: cy_spawn      cy_chan_recv_park    cy_maybe_yield
              │              │                    │
  Runtime: workers × fibers + cchan park/unpark + (optional) TLS current
```

---

## Target language (end state)

```c
#include "chan.h"   /* ClassyC runtime wrappers over cchan */

void worker(chan<int> ch) {
    for (int i = 0; i < 100; i++) {
        await ch.send(i);     // park if full — never block the OS worker forever
        // compiler may insert cy_maybe_yield() on the back-edge
    }
    ch.close();
}

int main(void) {
    auto ch = chan<int>(16);  // buffered capacity 16
    go worker(ch);            // spawn fiber (or thin wrapper)

    int sum = 0;
    for (;;) {
        int v;
        if (!await ch.recv(&v)) break;  // false after close+drain
        sum += v;
    }
    return sum == 4950 ? 0 : 1;
}
```

**Syntax sketch (v1 — keep C-shaped):**

| Construct | Meaning |
|-----------|---------|
| `go expr;` / `go f(args);` | Spawn fiber running `expr` / call |
| `await expr` | Evaluate at a **suspend point**; park if runtime says so |
| `chan<T>` / `chan_t*` | Typed handle over cchan (`msg_size = sizeof(T)`) |
| loop back-edge | Optional `cy_maybe_yield()` (fairness) |

Unbuffered `chan` + same-worker peer: **must** park via scheduler (try + yield), never raw blocking `cchan_send` between two fibers on one OS thread.

---

## Phase 0 — Foundations (done / in tree)

Checklist of what we already lean on:

- [x] Header-only **cchan** (`ext/ccchan/cchan.h`) — send/recv/try/budget/timeout/select/close
- [x] **minicoro** stackful coroutines (`ext/ccchan/minicoro.h`)
- [x] Smoke examples: `examples/classy-cchan.cy`, `examples/classy-cchan-fibers.cy`
- [x] `MCO_PTHREAD_TLS` workaround (pthread_key) for multi-worker until real TLS
- [x] AOT pipeline: `classyc-aot.sh` → `classyc -c` → `b2obj` → `gcc -lpthread -ldl -lm`
- [x] AOT glue: `src/mir-aot-runtime.c` auto-linked by `classyc-aot.sh`
- [x] Frontend already tracks `decl_spec.thread_local_p` / `N_THREAD_LOCAL` in `src/classyc.c`

---

## Phase 1 — Real `_Thread_local` (TLS)

### 1.0 Upstream MIR reality (re-check: https://github.com/vnmakarov/mir)

Upstream documents TLS as **out of scope for c2mir**, same bucket as atomics / VLA / complex:

> *c2mir … no optional standard features: variable size arrays, complex, atomic, **thread local variables***  
> — [c2mir/README.md](https://github.com/vnmakarov/mir/blob/master/c2mir/README.md)

| Fact | Detail |
|------|--------|
| Parse | `_Thread_local` is recognized (`T_THREAD_LOCAL` / `N_THREAD_LOCAL`) |
| Codegen | **Not implemented** — warning only; storage is ordinary static |
| Test | `c-tests/new/issue394.c` expects that warning in stderr |
| Bug report | [Issue #394 — Please implement `_Thread_local`](https://github.com/vnmakarov/mir/issues/394) (**open**, Mar 2024): same shared-cell bug we hit |
| MIR IR | Items are `data` / `bss` / `ref_data` / … only — **no TLS item type**, no TP-relative mem ops in [MIR.md](https://github.com/vnmakarov/mir/blob/master/MIR.md) |
| Threads in MIR | Multiple threads OK with **separate contexts** ([MIR.md](https://github.com/vnmakarov/mir/blob/master/MIR.md)); MIR does not manage OS threads ([#96](https://github.com/vnmakarov/mir/issues/96)) |

ClassyC inherits this from c2mir (`src/classyc.c` ~7340, same warning text as upstream).

**Implication for fibers:** we cannot “just enable” TLS in the frontend. Either:

1. **ClassyC fork of MIR/c2mir** implements TLS (likely — we already diverge), and/or  
2. **Avoid user-facing TLS** for the fiber runtime: keep `current` on the **worker struct**, use `pthread_key` only where C libs (minicoro) demand it, until (1) lands.

Do **not** plan on upstream shipping #394 on a fixed timeline.

### 1.1 Why we still want it

minicoro (and any multi-worker fiber runtime that uses `_Thread_local current`) needs **per-OS-thread** storage. Without it:

```text
warning -- Thread local is not implemented -- program might not work as assumed
// all pthreads share one cell → multi-worker mco assert
```

### 1.2 Language / API samples

```c
_Thread_local int tls_id;
static _Thread_local void *current_fiber;

void *thr(void *arg) {
    tls_id = (int)(intptr_t)arg;
    printf("%d @ %p\n", tls_id, (void *)&tls_id);  // distinct addresses per thread
    return 0;
}
```

Acceptance test = upstream issue #394’s program (`assert(x == 0)` in main after child sets `x = 1`).

After real TLS works, drop `MCO_PTHREAD_TLS` from fiber demos.

### 1.3 Compiler changes (`src/classyc.c` / c2mir lineage)

- [ ] **Stop treating TLS as a no-op** — remove/soften the warning once codegen works
- [ ] **Declaration codegen:** when `decl->decl_spec.thread_local_p`, do **not** emit plain `MIR_new_data` / `MIR_new_bss` as today
- [ ] **Use sites:** load/store via Tier A helpers or Tier B TLS addressing
- [ ] **Rules to keep** (already partially checked upstream):
  - no TLS on functions
  - `auto` + TLS invalid
  - v1: file-scope + `static` TLS is enough

Relevant sites (ClassyC): ~15295–15476, ~27919–27932, ~29131–29152; dump ~31507, ~31689.

### 1.4 MIR / codegen strategy (no free lunch from upstream)

Upstream MIR has **no** TLS primitive. Choose a tier **in the ClassyC tree** (`ext/mir` fork and/or classyc lowering only):

| Tier | Approach | JIT | AOT (`b2obj`) | Speed |
|------|----------|-----|----------------|-------|
| **A** | Lower `_Thread_local` → `pthread_key` + get/setspecific (or `__tls_get_addr`-style helper) | Frontend-only / small runtime | Easy (`-lpthread`) | ~ns/op, not free |
| **B** | Extend MIR: TLS data/bss items + TP/`%fs` access + load/link | `mir-gen-*.c`, module load | `.tdata`/`.tbss` + TLS relocs in `b2obj` | Near-native |

**Recommendation for FIBERS:**

- **Fiber runtime v1:** do **not** block on Tier B. Use **worker-local `current`** + existing `MCO_PTHREAD_TLS` for minicoro.  
- **User `_Thread_local` / clean multi-worker minicoro:** Tier **A** first (unblocks #394-class bugs without rewriting MIR-gen).  
- **Tier B** later if we want gcc-parity TLS and to contribute design notes upstream.

Tier A checklist (ClassyC, minimal MIR change):

- [ ] For each TLS object: allocate a `pthread_key_t` (process init) + size  
- [ ] Load → `call cy_tls_get(key)` / store → `cy_tls_set(key, val)` or memcpy for aggregates  
- [ ] `&tls_var` → address of per-thread cell (lazy alloc on first use)  
- [ ] AOT: helpers in `src/mir-aot-runtime.c` or `src/cyfiber.c`; JIT: `MIR_load_external`

Tier B checklist (deep MIR fork):

- [ ] `MIR_new_tls_data` / `MIR_new_tls_bss` (or `tls_p` on data/bss)  
- [ ] JIT: TLS image / IE model  
- [ ] x86_64: `fs:` / TP-relative mem  
- [ ] Document: no caching TLS addresses across `await`/yield  

### 1.5 AOT changes (`src/b2obj.c`, `classyc-aot.sh`)

Today `b2obj` emits only `.data` / `.bss` from MIR data/bss (`src/b2obj.c` ~598–771).

- [ ] **Tier A:** no ELF TLS sections required; link helpers + `-lpthread` (already in `classyc-aot.sh` `default_libs`)  
- [ ] **Tier B:** `.tdata` / `.tbss`, `R_X86_64_TPOFF*`, Mac path in `b2objmac.c`  

### 1.6 Verification (TLS)

```bash
# Same semantics as upstream issue #394
./bin/classyc examples/test-tls.cy -eg
./classyc-aot.sh examples/test-tls.cy -o /tmp/test-tls && /tmp/test-tls
```

`test-tls.cy`: child thread sets TLS; main still sees original value; addresses differ across threads.

---

## Phase 2 — Fiber runtime (library, no new syntax yet)

### 2.1 Layout

New files (suggested):

```text
include/cyfiber.h          # public API
src/cyfiber.c              # workers, runqueue, park, stacks
src/mir-aot-runtime.c      # optional: re-export or #include glue if needed for AOT
ext/ccchan/cchan.h         # unchanged substrate for messages
ext/ccchan/minicoro.h      # stack switch (v1), or later MIR-owned stacks
```

### 2.2 Public API sketch

```c
/* include/cyfiber.h */
typedef struct cy_fiber cy_fiber;
typedef struct cy_sched cy_sched;

typedef void (*cy_fiber_fn)(void *arg);

void       cy_sched_init(int n_workers);   /* 0 = default */
void       cy_sched_shutdown(void);

cy_fiber  *cy_spawn(cy_fiber_fn fn, void *arg);
void       cy_yield(void);                 /* explicit cooperative yield */
void       cy_park(void);                  /* sleep until unpark */
void       cy_unpark(cy_fiber *f);
cy_fiber  *cy_self(void);                  /* current fiber (TLS or worker slot) */

/* Channel park helpers (typed wrappers live in chan.h) */
int  cy_chan_send_park(cchan_t *c, const void *msg);  /* try → park → retry */
int  cy_chan_recv_park(cchan_t *c, void *msg);
```

Park path (must match fiber_workers rules):

```c
int cy_chan_recv_park(cchan_t *c, void *msg) {
    for (;;) {
        int rc = cchan_try_recv(c, msg);
        if (rc == 1) return 1;
        if (rc < 0) return 0;          /* closed empty */
        cy_park_on_chan_recv(c);      /* enqueue waiter, cy_yield to scheduler */
    }
}
```

### 2.3 Runtime checklist

- [ ] **Worker pool** — M pthreads, each with a local runqueue of fibers  
- [ ] **Fiber** — stack (minicoro v1), entry fn, state (Runnable / Parked / Dead)  
- [ ] **Current fiber** — prefer **worker-local pointer**; mirror in `_Thread_local` for C interop once TLS works  
- [ ] **Never** `mco_resume` a fiber on a different OS thread than it was created on (pin fibers to workers in v1)  
- [ ] **stop → close → join** lifecycle helpers for orderly shutdown  
- [x] **C11 atomics (Phase B)** — MIR opcodes + ClassyC `<stdatomic.h>` / `_Atomic` (seq_cst). Fiber demos may still use mutexes for channels; counters can use atomics (`CLASSY-ATOMICS.md`, `val-051-atomics.cy`)  
- [ ] Link: JIT host already has pthread; AOT via `classyc-aot.sh` already passes `-lpthread`

### 2.4 Compiler / MIR (Phase 2)

- [ ] Auto-import or preamble include of `cyfiber` runtime (like String/dict helpers) **or** explicit `#include "cyfiber.h"` in v1  
- [ ] AOT: compile `src/cyfiber.c` with **gcc** into an object linked by `classyc-aot.sh` (same pattern as `mir-aot-runtime.c`) so the runtime is not re-JITed incorrectly  

```bash
# classyc-aot.sh extension (conceptual)
# if fiber runtime enabled:
gcc -c -O2 -I include -I ext/ccchan src/cyfiber.c -o $workdir/cyfiber.o
link_objects+=("$workdir/cyfiber.o")
```

- [ ] JIT: either compile `cyfiber.c` as extra TU on the command line, or inject MIR imports bound to host symbols from a preloaded runtime  

### 2.5 Verification (runtime only)

```bash
./bin/classyc -I include -I ext/ccchan examples/classy-fiber-runtime.cy -eg
./classyc-aot.sh -I include -I ext/ccchan examples/classy-fiber-runtime.cy -o /tmp/fib && /tmp/fib
```

Port `classy-cchan-fibers.cy` logic onto `cy_*` API; multi-worker without `MCO_PTHREAD_TLS` once TLS lands.

---

## Phase 3 — Language: `go`, `await`, loop yields, `chan`

### 3.1 Keywords & AST (`src/classyc.c`)

- [ ] Soft keywords (expression/statement leading, like `move` / `new`):
  - `go` → `N_GO` (statement: `go call;` or `go { … }` later)
  - `await` → `N_AWAIT` (unary expression)
- [ ] Optional v1: `chan` as type constructor `chan<T>` via existing generics machinery **or** library-only `Chan<T>` class wrapping `cchan_t*`

**Parsing samples:**

```c
go worker(ch);           // N_GO(N_CALL(...))
go { f(); g(); };        // v2: N_GO(N_BLOCK(...))

x = await ch.recv();     // N_AWAIT(N_CALL(...))
await sleep_ms(10);      // N_AWAIT(...)
```

- [ ] `kw_add` entries; soft-keyword rules so `int go;` still works if needed  
- [ ] Check: `go` argument is call or void-returning thunk; `await` type is type of operand  

### 3.2 Lowering (check / gen)

#### `go f(args)`

```text
go f(a, b);
  →  /* heap or fiber-arg pack if needed */
     cy_spawn(wrapper, pack);
```

Wrapper for C ABI:

```c
/* synthesized */
static void __go_worker_1(void *p) {
    struct __go_args_1 *a = p;
    worker(a->ch);   /* original call */
    free(a);
}
```

- [ ] Capture args by value into a small struct when they outlive the spawner’s stack  
- [ ] Interact with `detach` / String arena: strings passed into `go` must be owned by the fiber or heap  

#### `await expr`

```text
await ch.recv(&v)
  → if expr is recognized park-op:
        cy_chan_recv_park(...)
     else:
        error: "await requires a parkable operation"   // v1 strict
```

v1 parkable set (whitelist):

- [ ] `chan` send/recv / select  
- [ ] `cy_sleep` / timer  
- [ ] `cy_join` (optional)  

#### Loop yields

At end of `for` / `while` / `do` / `for-in` body (gen already knows loop structure):

```text
loop_body:
  ...
  cy_maybe_yield();   // or every N iterations via static counter
  goto loop_header;
```

- [ ] Flag `-ffiber-yield-loops` / default on when any `go`/`await` in TU  
- [ ] Attribute or pragma to suppress on hot loops later  

### 3.3 Channels as language or library

**v1 recommendation:** library class + thin sugar, not full Go type system.

```c
// include/chan.h
class Chan<T> {
    cchan_t *raw;
    Chan(int cap) { raw = cchan_create((unsigned short)cap, (unsigned short)sizeof(T)); }
    ~Chan() { if (raw) cchan_dispose(raw); }
    int send(T v)   { return cy_chan_send_park(raw, &v); }
    int recv(T *o)  { return cy_chan_recv_park(raw, o); }
    void close()    { cchan_close(raw); }
};

// usage with await:
await ch.send(42);
await ch.recv(&x);
```

Later sugar:

```c
chan<int> ch = chan<int>(16);
ch <- 42;           // optional
x := <- ch;         // optional — bigger parse change
```

Checklist:

- [ ] `include/chan.h` wrapping cchan + park helpers  
- [ ] Generics: `Chan<T>` monomorphized (`sizeof(T)` → `msg_size`)  
- [ ] `close` / range-over-chan later (`for (auto v in ch)` until closed)  

### 3.4 Interaction with existing ClassyC features

| Feature | Rule at yield points |
|---------|----------------------|
| String arena | Treat `await` like a call boundary; keep return values; loop yields already sit next to per-iteration checkpoints |
| `defer` / `owned` | Unwind only on fiber exit / scope exit — not on park; parked fiber retains stack (stackful) |
| Exceptions | v1: exceptions do not propagate across fibers; uncaught in fiber → report + kill fiber |
| Safety guards | Unchanged inside a fiber; null-deref still catchable on that fiber’s stack |

- [ ] Document: **blocking syscalls without await stall the whole OS worker**  
- [ ] Document: no raw `cchan_send` between same-worker fibers  

### 3.5 Verification (language)

```bash
./bin/classyc -I include -I ext/ccchan examples/classy-go-chan.cy -eg
./classyc-aot.sh -I include -I ext/ccchan examples/classy-go-chan.cy -o /tmp/gochan && /tmp/gochan

# validation suite entry
cy-validate/val-0xx-fibers-go.cy
```

---

## Phase 4 — AOT end-to-end (`classyc-aot.sh`)

Pipeline today:

```text
classyc -c file.cy -o file.bmir
b2obj file.bmir file.o
gcc -no-pie -o prog file.o mir-aot-runtime.o -lm -lpthread -ldl
```

### 4.1 Checklist

- [ ] **Runtime objects always linked** when program uses fibers/TLS helpers:
  - `mir-aot-runtime.o` (existing)
  - `cyfiber.o` (new; host-compiled)
  - optional: prebuilt `cchan` only if not header-only in one TU  
- [ ] **`-I ext/ccchan -I include`** documented for AOT examples  
- [ ] **TLS Tier B:** `b2obj` TLS sections + relocs; verify under `readelf -W -l` / gdb  
- [ ] **DWARF `-g`:** stackful fibers may confuse unwinder — document; later: custom unwind info  
- [ ] **Multi-file AOT:** `classyc-aot.sh a.cy b.cy -o prog` — only one TU defines `CCHAN_IMPLEMENTATION` / runtime init  
- [ ] **Default libs** already include `-lpthread` — keep it  

### 4.2 Driver flags (optional)

```text
-ffiber              # enable go/await/loop yields
-fno-fiber-loop-yield
-fworkers=N          # default scheduler size (or runtime API only)
```

- [ ] Wire through `classyc` options and pass-through in `classyc-aot.sh` (`-f*` already forwarded)

### 4.3 AOT smoke

```bash
./classyc-aot.sh -I include -I ext/ccchan \
    examples/classy-go-chan.cy -o /tmp/classy-go
/tmp/classy-go
```

---

## Phase 5 — Hardening & polish

- [ ] **Select:** `await select { case … }` or `cy_select_park` over cchan_select  
- [ ] **Timeouts:** `await ch.recv_timeout(&v, ms)`  
- [ ] **Cancellation / context** (later)  
- [ ] **Work stealing** across workers (v1 pins fibers — no steal)  
- [ ] **Real atomics** in ClassyC (removes mutex counters in demos)  
- [ ] **LSP:** highlight `go`/`await`; don’t treat as identifiers when soft keywords  
- [ ] **cy-validate** matrix: TLS, multi-worker fibers, go+chan, AOT binary  
- [ ] Retire `MCO_PTHREAD_TLS` once Tier B TLS is default  

---

## Implementation order (summary checklist)

### A. TLS
1. [ ] Confirm strategy vs upstream: **c2mir will not give us TLS for free** ([#394](https://github.com/vnmakarov/mir/issues/394) open; c2mir README lists TLS as unimplemented)  
2. [ ] **Fiber path:** worker-local current + `MCO_PTHREAD_TLS` until user TLS works  
3. [ ] **Tier A** in ClassyC: lower `_Thread_local` → pthread_key helpers (fixes #394-class bugs)  
4. [ ] **Tier B** (optional later): MIR TLS items + `%fs` + `b2obj` `.tdata`/`.tbss`  
5. [ ] Tests: issue #394 program, JIT + `classyc-aot.sh`  

### B. Runtime
6. [ ] `include/cyfiber.h` + `src/cyfiber.c` (spawn/yield/park/workers)  
7. [ ] Channel park wrappers on cchan  
8. [ ] AOT link of `cyfiber.o` from `classyc-aot.sh`  
9. [ ] Example without language sugar  

### C. Language
10. [ ] Parse `go` / `await`  
11. [ ] Lower `go` → `cy_spawn`  
12. [ ] Lower `await` → park ops  
13. [ ] Loop back-edge `cy_maybe_yield`  
14. [ ] `Chan<T>` / `include/chan.h`  
15. [ ] Examples + `cy-validate`  

### D. Product
16. [ ] README + this `FIBERS.md` as living checklist  
17. [ ] AOT + JIT parity script in `examples/` or `cy-validate/`  

---

## Critical files

| Path | Role |
|------|------|
| `src/classyc.c` | Parse/check/gen: TLS, `go`, `await`, loop yields |
| `ext/mir/mir.h`, `mir.c`, `mir-gen-*.c` | TLS items + machine codegen |
| `src/b2obj.c`, `src/b2objmac.c` | AOT ELF/Mach-O TLS or helpers |
| `src/mir-aot-runtime.c` | Host helpers for AOT binaries |
| `classyc-aot.sh` | Link fiber runtime + pthread |
| `ext/ccchan/cchan.h` | Channel substrate |
| `ext/ccchan/minicoro.h` | Stack switch (v1) |
| `include/cyfiber.h`, `src/cyfiber.c` | Scheduler runtime (new) |
| `include/chan.h` | Typed channel wrapper (new) |
| `examples/classy-cchan*.cy` | Current library demos |
| `cy-validate/` | Executable specs |

---

## Non-goals (v1)

- Preemptive scheduling  
- Migrating a running fiber across OS threads  
- Full Go memory model / GC  
- Stackless `async`/`await` state machines (we stay **stackful** + known suspend points)  
- Making every C library call automatically async  

---

## Verification matrix (definition of done)

| Test | JIT | AOT |
|------|-----|-----|
| Distinct `_Thread_local` addresses across pthreads | ✓ | ✓ |
| Multi-worker fibers (no `MCO_PTHREAD_TLS`) | ✓ | ✓ |
| `go` + `await` channel pipeline | ✓ | ✓ |
| Loop yield prevents single-fiber starvation (smoke) | ✓ | ✓ |
| Close/drain semantics | ✓ | ✓ |
| No regress existing `cy-validate` | ✓ | optional |

Commands:

```bash
# JIT
./bin/classyc -I include -I ext/ccchan examples/classy-go-chan.cy -eg

# AOT
./classyc-aot.sh -I include -I ext/ccchan examples/classy-go-chan.cy -o /tmp/gochan
/tmp/gochan

# Full validation (once wired)
sh cy-validate/run-validate.sh
```

---

## One-sentence architecture

**Implement real TLS for multi-worker correctness; host a stackful fiber scheduler on cchan park/unpark; lower `go` / `await` / loop edges in the compiler so suspend points stay safe for arenas and ownership — same runtime linked for JIT and for `classyc-aot.sh`.**
