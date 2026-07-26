# ClassyC / MIR Thread-Local Storage (TLS)

Last updated: 2026-07-22

**Status:** Phase N1 complete; **N2 ELF local-exec AOT landed** (x86_64).
JIT still uses emulated `mir_tls_addr`; AOT (`b2obj`) uses `%fs` + `TPOFF32`.

Upstream MIR/c2mir lists `_Thread_local` as out of scope (same bucket as
atomics / VLA / complex). c2mir parses it, warns, and emits ordinary
static storage — all OS threads share one cell
([vnmakarov/mir#394](https://github.com/vnmakarov/mir/issues/394)).

This document is the **implementation contract and live progress log**.
Design rationale (strategies survey, MIR constraints): conversation notes;
fiber interaction: [`FIBERS.md`](FIBERS.md).

---

## Goals

1. **C11 `_Thread_local` / `static _Thread_local`** correct under ClassyC JIT
   (`-eg`) and AOT (`classyc-aot.sh`).
2. **First-class MIR TLS items** (not a frontend-only hack on normal data).
3. **JIT / interpreter:** MIR-owned TLS template + per-OS-thread copy
   (emulated / software TLS — realistic under MIR’s absolute-address JIT).
4. **AOT (later):** real ELF local-exec (`.tdata` / `.tbss` + `TPOFF`) when
   linking an executable.
5. Unblock ClassyC-compiled code that needs true TLS (e.g. minicoro without
   `MCO_PTHREAD_TLS`); issue #394-class tests pass.

Non-goals for N1:

* Full ELF GD/LD / `__tls_get_addr` / dlopen TLS
* Mach-O native TLS in `b2objmac` (AOT-Mac stays emulated until later)
* C++ `thread_local` destructors
* Fiber-local storage (TLS remains **OS-thread** local; fibers stay pinned)
* Injecting `PT_TLS` into a live JIT process

---

## Architecture summary

```text
  _Thread_local T x;          classyc / c2mir
         │
         ▼
  MIR_tls_data / MIR_tls_bss   (template + module_id + offset)
         │
    ┌────┴────────────────────────────┐
    │ JIT / interp                     │ AOT (N2)
    ▼                                  ▼
  mir_tls_register(template)         b2obj: .tdata/.tbss STT_TLS
  mir_tls_addr(mod, off)             text: %fs:off  (LE)
  → pthread_key → per-thread copy    ld/libc: PT_TLS clone
```

### Why not flag existing `data`/`bss`?

`load_bss_data_section` allocates one process-global blob and sets
`item->addr`. JIT code bakes that absolute VA via `get_ref_value`. TLS
addresses are **per-thread** (`base + offset`). So TLS needs its own item
types and address materialization, not a boolean on data/bss.

### Why helpers first (not `%fs` day one)?

Same lesson as LLVM ORC and as MIR atomics (builtins in machinize first):

* Works on every target without new mem addressing modes
* Interpreter + JIT share one runtime
* AOT can later replace the call sequence with TP-relative loads

---

## IR surface (N1)

| API | Role |
|-----|------|
| `MIR_new_tls_bss(ctx, name, len)` | Zero-fill TLS object (like `MIR_new_bss`) |
| `MIR_new_tls_data(ctx, name, el_type, nel, els)` | Initialized TLS object |
| Item types | `MIR_tls_data_item`, `MIR_tls_bss_item` |
| Load-time fields | `module_tls_id`, `offset` into module TLS image |
| Access | `mir_tls_addr(mod_id, offset)` → `void *` (or `mir_tls_base` + add) |

Textual MIR (sketch):

```text
x: tls_bss 4
y: tls_data i32 1
```

`item->addr` for TLS items is **not** the live cell (or is unused for loads).
Live address is always runtime `base + offset`.

### Runtime (host, gcc-compiled)

```c
void  mir_tls_register(uint32_t id, size_t size, size_t align, const void *tmpl);
void *mir_tls_base(uint32_t id);
void *mir_tls_addr(uint32_t id, size_t offset);  /* base + offset */
```

* One `pthread_key` → per-thread map/array of module bases
* First touch: allocate, `memcpy` template, zero tail
* Host may cache bases in real `_Thread_local` for speed
* Linked into `classyc` driver + `mir-aot-runtime.c` (AOT until N2 native)

### Frontend lowering

```text
_Thread_local int x = 1;

// item: MIR_new_tls_data("x", i32, 1, &init)
// use &x / load / store:
  t0 = call mir.tls_addr, imm(mod), imm(off)
  // ordinary mov/load/store through t0
```

---

## Phases & checklist

### Phase N1 — MIR TLS + emulated runtime (current)

| # | Task | Status | Notes |
|---|------|--------|-------|
| N1.1 | Extend `MIR_item_type_t` + `MIR_tls_*` structs in `mir.h` | **done** | `MIR_tls_data` / `MIR_tls_bss`, module TLS fields |
| N1.2 | `MIR_new_tls_data` / `MIR_new_tls_bss` in `mir.c` | **done** | |
| N1.3 | Module load: build TLS template, assign `id`/`offset`, call `mir_tls_register` | **done** | `load_module_tls` |
| N1.4 | Binary MIR read/write for TLS items | **done** | tags: `tlsbss`/`ntlsbss`, `tlsdata`/`ntlsdata` |
| N1.5 | Textual scan/output for `tls_data` / `tls_bss` | partial | **output** done; text **scan** still API-only |
| N1.6 | Host runtime `ext/mir/mir-tls.c` (+ decls in `mir.h`) | **done** | linked into `mir` object lib |
| N1.7 | Interpreter: resolve TLS refs via `mir_tls_addr` | **done** | load-time rewrite → CALL |
| N1.8 | mir-gen / load: TLS item refs → call `mir.tls_addr` | **done** | `lower_module_tls_refs` + gen safety net |
| N1.9 | classyc.c: emit TLS items when `thread_local_p`; materialize addr at uses | **done** | |
| N1.10 | Remove / gate “Thread local is not implemented” warning | **done** | |
| N1.11 | import_resolver / b2obj map / AOT link `mir-tls.o` | **done** | + `__mir_tls_aot_regs` bootstrap |
| N1.12 | Tests: issue #394 program JIT + AOT (emulated) | **done** | `examples/test-tls.cy`, `val-054-tls.cy` |
| N1.13 | Optional: ClassyC minicoro multi-worker without `MCO_PTHREAD_TLS` | **done** | `classy-cchan-fibers.cy`, `cyfiber.h` |

### Phase N2 — AOT ELF local-exec

| # | Task | Status | Notes |
|---|------|--------|-------|
| N2.1 | `b2obj`: emit `.tdata`, `STT_TLS`, offsets | **done** | Full template in `.tdata` (zeros for bss parts) |
| N2.2 | Object-mode gen: `R_X86_64_TPOFF32` + `%fs:0` LEA/ADD | **done** | `MIR_set_tls_native_aot` + x86_64 emit |
| N2.3 | `classyc-aot.sh`: native path (mir-tls.o still linked, unused for pure LE) | **done** | harmless |
| N2.4 | Verify with `readelf` TLS + #394 AOT | **done** | `.tdata`, `TPOFF32`, runtime pass |
| N2.5 | Mach-O path (`b2objmac`) | deferred | Emulated / not native LE yet |

### Phase N3 — Polish (optional)

| # | Task | Status | Notes |
|---|------|--------|-------|
| N3.1 | Hoist `mir_tls_base` in mir-gen hot paths | pending | |
| N3.2 | Multi-module TLS image merge for multi-file AOT | pending | |
| N3.3 | Drop `MCO_PTHREAD_TLS` from `cyfiber.h` if host minicoro uses real TLS only | **done** | Host gcc real TLS |

### Phase N4 — Full ABI (optional, not scheduled)

* GD/IE for shared libraries, import of foreign TLS symbols, dlopen

---

## Critical files

| Path | Role |
|------|------|
| `ext/mir/mir.h` | Item types, constructors |
| `ext/mir/mir.c` | Create/load/link/unload/binary IO |
| `ext/mir/mir-interp.c` | Interp TLS address |
| `ext/mir/mir-gen.c` (+ `mir-gen-*.c` if needed) | Ref lowering / no absolute bake |
| `src/classyc.c` | Frontend: `thread_local_p` → TLS items |
| `ext/mir/mir-tls.c` (new) | Host emulated TLS runtime (in mir lib) |
| `src/mir-aot-runtime.c` | Export runtime for AOT |
| `src/classyc-driver.c` | Bind imports under JIT |
| `src/b2obj.c` | AOT ELF (N1: skip TLS items; N2: `.tdata`/`.tbss`) — **this is the built tool** |
| `src/b2objmac.c` | AOT Mach-O (N1: skip TLS items) |
| `ext/mir/b2obj.c` | Vendored copy (kept in sync for N1 skip; not the CMake target) |
| `CMakeLists.txt` | Compile/link `mir-tls.c` |
| `examples/test-tls.cy` / `cy-validate/val-0xx-tls.cy` | Acceptance |

---

## Verification matrix

| Test | JIT (`-eg`) | AOT emulated | AOT native (N2) |
|------|-------------|--------------|-----------------|
| issue #394: child sets TLS, main still 0 | N1 | N1 | N2 |
| Distinct `&tls_var` across pthreads | N1 | N1 | N2 |
| Initialized `_Thread_local` | N1 | N1 | N2 |
| Aggregate TLS (`struct`, array) | N1 | N1 | N2 |
| `static _Thread_local` inside function | N1 | N1 | N2 |
| Multi-worker fibers without `MCO_PTHREAD_TLS` in .cy | N1.13 | — | — |
| No regress `cy-validate` | always | always | always |

### What you can test **now** (N1 runtime + IR)

```bash
# Unit test: TLS items, mir_tls_addr, issue #394 semantics, binary IO
cmake --build build --target tls_test
./build/bin/tls_test
# or:
cd build && ctest -R tls-test --output-on-failure
```

### After frontend lands (N1.9+)

```bash
./bin/classyc examples/test-tls.cy -eg
./classyc-aot.sh examples/test-tls.cy -o /tmp/test-tls && /tmp/test-tls
sh cy-validate/run-validate.sh
```

---

## Progress log

### 2026-07-22

* Wrote this plan from MIR source review + #394 / c2mir README.
* Strategy: **N1 emulated first-class TLS**, **N2 AOT ELF LE**, not pure
  frontend pthread_key and not full GD.
* **N1.1–N1.4, N1.6 done:** MIR TLS items, load/register, binary IO, output,
  `ext/mir/mir-tls.c` (pthread_key + per-thread bases), CMake links pthread.
* `b2obj` skips TLS items in N1 (emulated path; N2 ELF LE later).
* Confirmed **`src/b2obj.c`** (CMake target) has `MIR_tls_*` skip cases;
  also `src/b2objmac.c` and vendored `ext/mir/b2obj.c`. Rebuilt `bin/b2obj`.
* Build: `mir_static` + `classyc` + `b2obj` compile clean.
* Added **`ext/mir/mir-tests/tls.c`** → target `tls_test` (passes).
* **N1 frontend complete:** classyc emits `MIR_tls_*`; load rewrites refs to
  `mir.tls_addr`; b2obj emits templates + `__mir_tls_aot_regs`; AOT links
  `mir-tls.o`.
* **Verified:** `examples/test-tls.cy` and `val-054-tls.cy` pass JIT **and** AOT.
* **N1.13 done:** dropped `MCO_PTHREAD_TLS` from `examples/classy-cchan-fibers.cy`
  and host `cyfiber.h`; multi-worker smoke passes with ClassyC `_Thread_local`.
* **Critical fix:** removed `#define __STDC_NO_THREADS__` from `include/mirc.h`.
  With that macro set, minicoro skipped `_Thread_local` and used a plain static
  `mco_current_co` (corrupt multi-worker).  Full `<threads.h>` thrd_* API is
  still not provided; only TLS is advertised as available.
* Docs: `FIBERS.md` / `README.md` updated for shipped TLS.
* **N2 done (x86_64):** `MIR_set_tls_native_aot` before load in `b2obj`;
  gen emits `mov %fs:0; add $sym@tpoff`; object has `.tdata` + `STT_TLS` +
  `R_X86_64_TPOFF32`. Linked binary has `PT_TLS`. JIT unchanged (emulated).
* **Next:** Mach-O LE (optional); multi-TU TLS image merge polish; drop
  optional `mir-tls.o` from AOT link when no emulated refs remain.
* **Runtime arenas are now per-thread:** `cstring.h` (String registry),
  `cobjarena.h` (object registry) and `cyexc.h` (try/catch frame stack) keep
  their state in C11 `_Thread_local`.  Checkpoint/release stay lock-free —
  each OS thread owns an independent positional stack.  Thread-exit leftovers
  are swept by a pthread TSD destructor (main thread still via `atexit`).
  Cross-thread String handoff: `c2m_str_detach` (sender) + `c2m_str_attach`
  (receiver); verified by `cy-validate/val-055-tls-arena.cy` (JIT + AOT).
  `mir-aot-runtime.c` now also includes `cobjarena.h` (was missing for AOT).
  Note: per-thread ≠ per-fiber — fibers on one OS thread still share that
  thread's arena.

---

## Decision record

| Decision | Choice | Why |
|----------|--------|-----|
| IR representation | New `tls_data` / `tls_bss` items | Absolute `item->addr` model is wrong for TLS |
| JIT access | `mir_tls_addr` / `mir_tls_base` helpers | Portable; matches MIR call/ref patterns |
| AOT end state | ELF LE via b2obj | Real native for production binaries |
| Default mode | auto: JIT emulated, AOT native when N2 lands | Best of both |
| Fiber “current” | Unchanged (host minicoro / worker pin) | Does not depend on language TLS |
| Upstream | Implement in ClassyC’s MIR fork | #394 open with no design |

---

## One-sentence architecture

**Add first-class MIR TLS items with a module template and helper-based
per-thread addresses for JIT/interp; later emit real ELF local-exec from
b2obj for AOT — so `_Thread_local` is C11-correct without waiting on
upstream or inventing process-wide PT_TLS injection.**
