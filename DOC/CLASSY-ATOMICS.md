# ClassyC / MIR atomics

Last updated: 2026-07-16

MIR has no atomic IR today. c2mir documents atomics as out of scope (same
bucket as TLS / complex / VLA). llvm2mir hard-errors on `Fence` / `CmpXchg` /
`AtomicRMW`. ClassyC fiber demos still use mutexes for counters until this
lands ([`FIBERS.md`](FIBERS.md)).

This document is the implementation plan and the contract for the first cut.

---

## Goals (v1)

1. **First-class MIR opcodes** for integer atomics (not special `MOV`s).
2. **Correct under `-O2`**: optimizers must not CSE / DSE / LICM atomics.
3. **Interpreter + JIT** on x86_64 and aarch64 (host of this tree).
4. Enough for fiber counters, refcounts, and simple lock-free flags.
5. Leave ClassyC / c2mir frontend emission as a **follow-on** once IR+gen work.

Non-goals for v1:

* 128-bit CAS (can “fake” later with lo/hi + `cmpxchg16b`; no `i128` type)
* FP atomics, `_Atomic` aggregates larger than 8 bytes
* Full C11 order lattice on every op (v1 is **seq_cst** semantics)
* Native machine patterns (v1 lowers to host `__atomic_*` builtins in machinize)

---

## Design summary

```
Front-end (later)  →  MIR atomic opcodes  →  simplify (keep mem)  →
  gen opts (barriers)  →  target_machinize (→ CALL builtins)  →  RA / emit
Interpreter: direct __atomic_* on simplified base-only mem
```

### Why not special `MOV`?

Ordinary loads/stores are `MIR_MOV` with a mem operand. DSE, GVN mem-availability,
and LICM all treat moves as optimizable. Encoding atomics as `MOV` would silently
break concurrent code at `-O2`.

### Why builtins in machinize (v1)?

MIR already lowers awkward ops (`va_arg`, long-double, big block copies) to
`_MIR_builtin_func` calls in `target_machinize`. That path:

* works on every target gen file with one shared helper
* matches interpreter semantics (host `__atomic_*`)
* avoids RA constraints for `lock cmpxchg` / LSE on day one

Native patterns (`lock xadd`, `ldar`/`stlr`, …) are a later optimization; the
opcodes stay the same.

---

## IR surface

Added before `MIR_INVALID_INSN` / `MIR_INSN_BOUND` in [`ext/mir/mir.h`](ext/mir/mir.h):

| Opcode | Text form | Semantics (seq_cst) |
|--------|----------|----------------------|
| `MIR_ALOAD` | `dst, mem` | `dst = atomic_load(mem)` |
| `MIR_ASTORE` | `mem, val` | `atomic_store(mem, val)` |
| `MIR_AFENCE` | *(none)* | full seq_cst fence |
| `MIR_AXCHG` | `old, mem, val` | `old = atomic_exchange(mem, val)` |
| `MIR_AADD` | `old, mem, val` | `old = atomic_fetch_add(mem, val)` |
| `MIR_ASUB` | `old, mem, val` | `old = atomic_fetch_sub(mem, val)` |
| `MIR_AAND` | `old, mem, val` | `old = atomic_fetch_and(mem, val)` |
| `MIR_AOR` | `old, mem, val` | `old = atomic_fetch_or(mem, val)` |
| `MIR_AXOR` | `old, mem, val` | `old = atomic_fetch_xor(mem, val)` |
| `MIR_ACAS` | `old, mem, expected, desired` | strong CAS; `old` = previous `*mem` |

**Width** comes from the mem operand type: `i8`/`u8`/`i16`/`u16`/`i32`/`u32`/`i64`/`u64`
(and `p` as pointer-sized). No float/LD atomics in v1.

**Helper:** `MIR_atomic_code_p(code)` for optimizers and simplify.

**Memory order (future):** either order immediates or opcode families
(`ALOAD_ACQ`, …). v1 always uses `__ATOMIC_SEQ_CST`.

**128-bit CAS (future):** not an `i128` type — pair of `i64` + machinize to
`cmpxchg16b` / `CASP` (the classic “fake it” pattern).

---

## Critical MIR plumbing

### 1. `simplify` must not turn atomics into plain loads/stores

In `simplify_op` (`mir.c`), non-move memory operands are normally rewritten into
ordinary `MOV` loads/stores. That would destroy atomicity.

Atomic mem operands must stay **simplified mem** (base-only, `disp=0`, no index),
same shape the interpreter already requires for `MOV` mem ops after simplify.

### 2. Optimizer barriers (`mir-gen.c`)

Treat atomics like calls for memory / placement:

| Pass | Rule |
|------|------|
| `fixed_place_insn_p` | include all atomic codes (no GVN/LICM move) |
| mem availability / GVN | clear available mem (like `MIR_call_code_p`) |
| DSE | only `move_code_p` stores are deleted — atomics stay safe if not moves |
| LICM / pressure | do not hoist atomics |
| `move_code_p` / `move_p` | false |

### 3. Machinize → builtins

Shared helpers (in gen, registered via `_MIR_builtin_func`):

```c
uint64_t mir_atomic_load  (void *p, uint64_t size);
void     mir_atomic_store (void *p, uint64_t v, uint64_t size);
uint64_t mir_atomic_xchg  (void *p, uint64_t v, uint64_t size);
uint64_t mir_atomic_fetch_add / sub / and / or / xor (...);
uint64_t mir_atomic_cas   (void *p, uint64_t expected, uint64_t desired, uint64_t size);
void     mir_atomic_fence (void);
```

`size` is 1/2/4/8. Each atomic MIR insn becomes address materialization (if
needed) + `MIR_CALL` of the matching helper; original insn deleted. Sets
`leaf_p = FALSE`.

### 4. Interpreter

`mir-interp.c`: after simplify, mem is base-only. Execute with `__atomic_*`
using the mem type for width.

### 5. Targets

`mir-gen-x86_64.c` and `mir-gen-aarch64.c` (and other target files) call the
shared machinize helper in `target_machinize`. No pattern-table entries required
for v1.

---

## Implementation phases

### Phase A — IR + core (this change set)

- [x] Design doc (`CLASSY-ATOMICS.md`)
- [x] `mir.h` opcodes + `MIR_atomic_code_p`
- [x] `mir.c` `insn_descs` + simplify keep-mem for atomics
- [x] `mir-interp.c` execution
- [x] `mir-gen.c` barriers
- [x] Shared builtin helpers + machinize expand (all targets that include gen)
- [x] Smoke test (`ext/mir/mir-tests/atomics.c` — interp + gen `-O2` OK)

### Phase B — Frontend

- [x] ClassyC: emit `ALOAD`/`ASTORE`/RMW for `_Atomic` load/store/`++`/`+=`/`&=`/…
- [x] ClassyC: `__atomic_*` builtins → MIR atomics (seq_cst; order args ignored)
- [x] Built-in `<stdatomic.h>` (`include/mirc_stdatomic.h`); drop `__STDC_NO_ATOMICS__`
- [x] Validate: `cy-validate/val-051-atomics.cy` (interp + `-eg` `-O2`)
- [x] x86_64 native ALOAD/ASTORE/AADD/ASUB/AXCHG/AFENCE (no host CALL; minicoro-safe)
- [x] Post-RA DCE keeps atomic insns (unused `fetch_add` result is still a side effect)
- [x] Cond-context: leave `true_label` for atomic loads (`while (!atomic_load(...))`)
- [x] `examples/classy-cchan-fibers.cy` uses `<stdatomic.h>` (matches `fiber_workers.c`)
- [ ] llvm2mir: map `AtomicRMW` / `CmpXchg` / `Fence` (optional)

### Phase C — Native lowerings (optional)

- [ ] x86_64: `lock xadd` / `xchg` / `lock cmpxchg` / TSO load-store
- [ ] aarch64: `ldar`/`stlr`, LSE `ldadd`/`cas`, or LL/SC expand
- [ ] Keep builtin fallback for exotic sizes / missing features

### Phase D — Widen

- [ ] Memory orders beyond seq_cst
- [ ] 128-bit CAS via lo/hi pairs
- [ ] mir2c pretty-print if needed

---

## Testing plan

1. **Interpreter unit:** load/store/add/cas on shared `int64_t` buffer.
2. **JIT unit:** same via `MIR_gen` on host arch.
3. **`-O2` smoke:** ensure adjacent ordinary loads are not CSE’d across `ASTORE`.
4. Later: multi-thread hammer (fetch_add counters) under `-eg` / fibers.

---

## Files touched

### Phase A (MIR IR + gen)
| File | Change |
|------|--------|
| `CLASSY-ATOMICS.md` | this plan |
| `ext/mir/mir.h` | opcodes + helper |
| `ext/mir/mir.c` | descs, simplify |
| `ext/mir/mir-interp.c` | execute |
| `ext/mir/mir-gen.c` | barriers; include shared atomic gen |
| `ext/mir/mir-gen-atomic.c` | builtins + `machinize_atomic_insn` |
| `ext/mir/mir-gen-{x86_64,aarch64,ppc64,riscv64,s390x}.c` | call machinize helper |
| `ext/mir/mir-tests/atomics.c` | MIR API smoke |

### Phase B (ClassyC frontend)
| File | Change |
|------|--------|
| `src/classyc.c` | gen ALOAD/ASTORE/RMW; `__atomic_*` builtins |
| `include/mirc.h` | register `stdatomic.h`; no `__STDC_NO_ATOMICS__` |
| `include/mirc_stdatomic.h` | C11-ish header over MIR atomics |
| `cy-validate/val-051-atomics.cy` | load/store/fetch_add/++/CAS/exchange |

---

## Notes for reviewers

* **Seq_cst only** is intentional: enough for correctness-first fiber work; order
  operands can be added without renumbering if placed carefully (prefer new
  opcodes or trailing imm later).
* Builtin calls are **not** a permanent performance ceiling; they are the MIR-idiomatic
  way to ship a correct v1 on all backends.
* Frontend emission is deliberately last: without gen barriers, emitting atomics
  as plain stores would be worse than not emitting them.
