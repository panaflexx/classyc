# GEN-OPT Findings — Phase A–D

Last updated: 2026-07-16

Implements Phase A (instrumentation / baselines), Phase B (midopt P0/P1), and
Phase C/D gen open-code (dense accessors + dense for-in) from
[`GEN-OPT.md`](GEN-OPT.md). Midopt lives in **`src/midopt.c`**, included into
`classyc.c` the same way as `src/ownership.c` (not a separate CMake TU).

---

## 1. What landed

| Item | Location | Notes |
|------|----------|--------|
| Midopt pass | `src/midopt.c` | `#include`d after `ownership.c` |
| Pipeline | `c2mir_compile` | `check → ownership → midopt → gen` |
| Timing | `-v` | `midopt=` usec in the stage line |
| Flags | `include/classyc.h`, driver | `-fno-midopt`, `-fmidopt`, `-fdump-mir-stats` |
| P0 dead methods | `midopt_dead_p` on `decl` | Skip body + MIR forward for dead **class methods** |
| P1 OOB elision | `elide_oob_p` on `expr` | Const index into static array → no OOB trap |
| P1 null | `own_deref_class = SAFE` | `this` receiver |
| MIR stats | `-fdump-mir-stats` | funcs / forwards / insns / calls |
| Sketch tests | `sketch/sketch-midopt-*.cy`, `run-midopt-bench.sh` | |
| `-O` plumbing | already present | `MIR_gen_set_optimize_level` when `-O`/`-O0`… given |

**Default:** midopt **on**. Free functions are never pruned (C linkage).

---

## 2. Phase A — baselines (`sketch/sketch-midopt-list-sum.cy`)

Workload: stack `List<int>`, 100× `Add`, for-in sum, `Count`.  
Machine: local Linux tree, `./bin/classyc -I include … -eg`.

### 2.1 MIR shape (after gen, before JIT)

| Config | funcs | insns | calls | `.bmir` size |
|--------|------:|------:|------:|-------------:|
| **midopt on (default)** | **81** | **2530** | **424** | **18630** |
| `-fno-midopt` | 147 | 4729 | 863 | 27984 |
| Ratio (midopt / off) | **0.55** | **0.53** | **0.49** | **0.67** |

Midopt report for this TU: **class methods=138 kept=72 dead=66**  
(before class-expand seeds ≈ 6: ctor/dtor/Add/Count/Get/EnsureCapacity on `List<int>`).

### 2.2 Compile stage times (`-v`, illustrative)

| Stage | midopt on (usec) | midopt off (usec) |
|-------|-----------------:|------------------:|
| preprocess | ~34–38k | similar |
| parse | ~12–14k | similar |
| check | ~9–10k | similar |
| ownership | ~12k | similar |
| **midopt** | **~5k** | 0 |
| generate | **~6–7k** | **~22k** |
| total | ~85k | ~92–96k |

Generate is ~**3× faster** when dead monomorph methods are not lowered. Midopt
itself is cheap (~5 ms) relative to preprocess.

### 2.3 Release flags (confirmed)

| Flag | Role |
|------|------|
| (default) | exceptions **on**, midopt **on**, MIR opt level **2** (MIR default) |
| `-fno-exceptions` | no safety traps / try; fewer calls (see below) |
| `-O` / `-O2` / `-O3` | `MIR_gen_set_optimize_level` in driver when `optimize_level >= 0` |
| `-fno-midopt` | disable P0/P1 midopt |
| `-fdump-mir-stats` | print `[mir-stats]` after gen |
| `-v` | stage timings + midopt keep/dead summary |

`-fno-exceptions -O2` with midopt on the same sketch (measured):

| Config | funcs | insns | calls |
|--------|------:|------:|------:|
| midopt + exceptions (default) | 81 | 2530 | 424 |
| midopt + `-fno-exceptions -O2` | 81 | **2055** | **285** |

Same func count (method set unchanged); ~19% fewer insns and ~33% fewer calls
from eliding safety traps.

### 2.4 `-O` plumbing

In `classyc-driver.c`: `optimize_level` defaults to `-1` (leave MIR at default
**2**). If the user passes `-O` / `-On`, driver calls
`MIR_gen_set_optimize_level` after `MIR_gen_init`. **Verified** accepted for
`-O2` / `-O3` on sketch programs.

---

## 3. Phase B — midopt design (what works)

### 3.1 P0 reachability (final algorithm)

1. **Seed only free-function bodies** (not monomorph method bodies).  
2. Mark `def_node` → `N_FUNC_DEF` methods, RAII ctor/dtor calls, **for-in
   protocol** (`Count`/`Get`/`KeyAt`/`ValAt`) — for-in resolves those only at
   **gen** time, so midopt must special-case `N_FORIN`.  
3. Worklist over kept method bodies (intra-class callees via `def_node`).  
4. **Class expand once:** if any method of a monomorph class is kept, keep
   **all** methods of that class (safe for incomplete `this→sibling` graphs).  
5. Optional extra worklist **without** further whole-class expand (avoids
   pinning unused monomorphs via residual AST edges).  
6. Remaining class methods → `midopt_dead_p`; gen skips body + forward.

**Why not seed from whole-module / all `used_p`:** checking monomorph method
bodies sets `used_p` / `def_node` on almost every intra-class edge; seeding from
that pins every method of every specialization.

**Why class expand:** gen-time protocols and incomplete stamps still leave
holes (e.g. early per-method DCE dropped `Get` for for-in → SEGV). Expand
trades some size for correctness inside a live monomorph while still dropping
**unused specializations** (`List<String>` when only `List<int>` is used → ~half
the class methods in this sketch).

### 3.2 P1 safety elision

- **OOB:** `a[const]` on a fixed-length C array with `0 <= i < n` →
  `elide_oob_p`; gen skips `gen_oob_check`.  
- **Null:** `this` as receiver → `DEREF_GUARD_SAFE`.  
- Sketch: `sketch/sketch-midopt-oob-elide.cy` → `PASS`.

### 3.3 Correctness smoke

| Test | Result |
|------|--------|
| `sketch/sketch-midopt-list-sum.cy` | PASS (midopt / no-midopt) |
| `sketch/sketch-midopt-oob-elide.cy` | PASS |
| `val-038`, `042`, `043`, `044`, `046`, `049`, `050` | exit 0, no unresolved |

Gen soft-guard: reference to a midopt-dead method with null MIR item warns
instead of asserting on `N_MEMBER` (residual AST edges).

---

## 4. Pitfalls discovered

1. **Unsafe attr casts** — early midopt treated arbitrary `node->attr` as
   `struct expr *` and could corrupt decls. Fixed: only known expression
   node codes.  
2. **Seeding from method bodies** — pins entire monomorph graphs. Free-func
   seed only.  
3. **for-in protocol** — `Count`/`Get` not always on AST `def_node`; must
   mark in midopt via `find_class_protocol_method`.  
4. **Whole-class expand after scanning expanded bodies** — re-introduced
   unused monomorphs (`List<String>` via residual branches). Expand once;
   limited follow-up worklist.  
5. **`used_p` at method resolve** — check now sets `used_p` when resolving
   instance/static methods, Get/GetMut, ctor/dtor (helps any future analysis).

---

## 5. Phase C/D — dense open-code (gen)

### 5.1 What landed

| Item | Location | Behavior |
|------|----------|----------|
| Dense field detect | `find_dense_buffer_fields` | List: `data`+`length`; Set: `dense`+`count`; Map excluded when `keys`+`vals` present |
| Predicate | `dense_accessor_open_codeable_p` | Pure (no MIR); used before N_CALL eval |
| Open-code emit | `try_open_code_dense_accessor` | Field loads + optional OOB/null traps |
| Protocol hook | `gen_class_method_call_dest` | Prefer open-code for for-in / brace-init protocol calls |
| N_CALL intercept | `case N_CALL` | Only when predicate true → single eval → open-code; else normal path |
| Dense for-in | `N_FORIN` class branch | `n=length`, `el=*(data+i)` (List/Set); Map still KeyAt/ValAt |

**Open-coded methods (scalar/pointer elements unless noted):**

| Layout | Methods |
|--------|---------|
| List / Set | `Count`, `IsEmpty`, `Capacity`, `Get`, `GetMut`, `First`, `Last`, `FirstMut`, `LastMut` |
| Map | `Count`, `IsEmpty`, `KeyAt`, `ValAt`, `ValMut` |

**Not open-coded (by design):**

- `Get` / `KeyAt` / `ValAt` / `First` / `Last` when element is class/struct/union (need aggregate return / `Copy`)
- Mutators (`Add`, `Set`, …)
- Non-dense classes that merely define `Count` (use normal `proto_item` path)

### 5.2 Correctness rule (N_CALL)

Do **not** fall back to `gen_class_method_call` for ordinary N_CALLs when
open-code fails. That helper builds a fresh `__methprotoN` via
`collect_args_and_func_types` and can disagree with the method’s stored
`proto_item` → MIR error *“number of call operands or results does not
correspond to prototype”*. Non-dense receivers must fall through to the
normal call path.

Double-eval hazard: only intercept when the predicate guarantees open-code
will emit (no partial MIR, no re-eval of GetMut chains).

### 5.3 Smoke / metrics

| Test | Result |
|------|--------|
| `cy-validate` (52 files) | **52 passed, 0 failed, 0 crashed** |
| `sketch/sketch-midopt-list-sum.cy` | `sum=4950 count=100` PASS |
| List First/Last/GetMut + Map KeyAt/ValAt/ValMut | PASS |
| Set Count/First/Last/Get + dense for-in | PASS |
| `ST2000_SECONDS=2` `-fno-exceptions -O2` | ~**82k ticks/sec** (wall fixed by env) |

list-sum MIR (illustrative, after open-code + dense for-in):

| Config | funcs | insns | calls |
|--------|------:|------:|------:|
| midopt + open-code | 81 | ~2529 | ~421 |
| `-fno-midopt` | 147 | ~4728 | ~860 |

Call count drop vs Phase B alone is modest at module level (Add and method
bodies still dominate); the hot path in for-in / `Count`/`Get` is field loads
instead of CALL.

### 5.4 Long-running bench

`examples/classy-space-trader-2000.cy` — galaxy sim (List/Map/Set, for-in,
Where/Find/Sort). Marked `@expect: skip` for the example runner (default 60s).

```bash
ST2000_SECONDS=2 ./bin/classyc -I include -fno-exceptions -O2 \
  examples/classy-space-trader-2000.cy -eg
# primary metric: ticks/sec
```

---

## 6. Gaps / next

| Gap | Direction |
|-----|-----------|
| Live monomorph still emits **all** methods | Method-level DCE; stop midopt-keeping Count/Get when all sites open-code |
| `List<String>` monomorph still *created* by headers | Avoid specializing unused `List<T>` in the front end |
| Open-code aggregate `Get` | Optional block-move into caller slot (for-in already does this densely) |
| Safety traps on `Add` / mutators | Prove capacity / open-code growth path; `-fno-exceptions` for speed profile |
| AOT DCE after BMIR | `b2obj` reachability (GEN-OPT Phase E) |
| Midopt and ownership CFG share | Cache when midopt grows flow-sensitive |

---

## 7. How to reproduce

```bash
# build
make classyc -j$(nproc)

# correctness + stats
./bin/classyc -I include -v -fdump-mir-stats sketch/sketch-midopt-list-sum.cy -eg
./bin/classyc -I include -v -fdump-mir-stats -fno-midopt sketch/sketch-midopt-list-sum.cy -eg

# AOT size
./bin/classyc -I include -c -o /tmp/midopt-list.bmir sketch/sketch-midopt-list-sum.cy
./bin/classyc -I include -fno-midopt -c -o /tmp/nomidopt-list.bmir sketch/sketch-midopt-list-sum.cy
ls -la /tmp/midopt-list.bmir /tmp/nomidopt-list.bmir

# harness
sh sketch/run-midopt-bench.sh

# Phase C/D dense open-code
./bin/classyc -I include sketch/sketch-midopt-list-sum.cy -eg
ST2000_SECONDS=2 ./bin/classyc -I include -fno-exceptions -O2 \
  examples/classy-space-trader-2000.cy -eg
sh cy-validate/run-validate.sh ./bin/classyc
```

---

## 8. Summary

Phases A–D are **in tree and useful**:

- Midopt is a real compiler stage in an included `.c` file (`midopt.c`).  
- On a minimal List sketch, **~2× fewer MIR funcs/insns/calls**, **~1.5× smaller
  BMIR**, **~3× faster generate**, with full `cy-validate` green (52/0/0).  
- P1 const OOB elision works for C arrays.  
- Gen open-codes dense List/Set/Map accessors and dense for-in (no Count/Get
  call in the loop body for List/Set).  
- Remaining size gap is intentional conservatism (class-level keep for live
  monomorphs) plus unused specializations still instantiated by headers.

Recommended default product flags remain **safe**: midopt on, exceptions on.  
Speed profile for benches: `-O2 -fno-exceptions` (and midopt left on).

---

## 8. Runtime: `examples/classy-aurora-ops.cy` (2026-07-16)

Showcase program (small fleet N≈8, lots of LINQ / Map / String / JSON demos).
Full wall time of `classyc -I include … -eg` = **preprocess + check + midopt +
MIR gen of every used function + run main**. Pure “algorithm” time is tiny;
`-v` shows **MIR link/gen ~0.8–0.9 s** alone.

### 8.1 Compile IR ( `-c -fdump-mir-stats` )

| Config | class methods | funcs | insns | calls | generate (µs) | `.bmir` |
|--------|--------------:|------:|------:|------:|--------------:|--------:|
| midopt **on** | 615 → **550 keep / 65 dead** | **599** | **20057** | **3884** | ~147k | **110 500** |
| `-fno-midopt` | (all) | 664 | 22422 | 4392 | ~161k | 123 682 |
| Δ | −11% methods | **−10%** | **−11%** | **−12%** | ~−9% | **−11%** |

So on a real multi-collection example, P0 still wins ~**10%** IR/size (less
than the tiny List-only sketch’s 2×, because most monomorphs are actually
touched).

### 8.2 Wall clock `-eg` (successful runs only, 5 tries each)

Intermittent **SEGV (exit 139)** mid-output (~line 55) occurs in **both**
midopt on and off — treated as a pre-existing flaky bug, not a midopt
regression. Numbers below are **ok runs only** (completed to `SHUTDOWN`).

| Config | ok/5 | best | avg (ok) |
|--------|-----:|-----:|---------:|
| midopt ON (default) | 3 | **1.08 s** | 1.15 s |
| midopt OFF | 3 | 1.19 s | 1.26 s |
| midopt ON + `-fno-exceptions -O2` | 4 | **0.87 s** | 0.89 s |
| midopt OFF + `-fno-exceptions -O2` | 2 | 0.95 s | 0.97 s |

**Rough runtime/wall takeaway:**

- Midopt vs not: ~**8–10%** faster end-to-end `-eg` (best and avg).  
- That tracks **fewer functions to JIT at link**, not hotter LINQ loops.  
- `-fno-exceptions -O2` is the bigger lever (~**20–25%** vs default safe), again
  mostly fewer traps/calls in generated code + MIR `-O2`.  
- Midopt + fast flags stack: **~1.08 s → ~0.87 s** (~20% vs default midopt).

### 8.3 Pure “ops” runtime

Not isolated: the program does not loop enough for a stable microbench, and
eager `-eg` JITs the whole call graph before `main` returns useful work. A
future fair pure-runtime test would:

1. AOT (`classyc -c` → `b2obj` → link) and `time ./aurora`, or  
2. Add an internal iteration count (e.g. 100k× `Where`/`Select` on a fixed fleet)
   and time only that loop.

Until then: **aurora-ops shows a modest wall-time win from midopt (~10%),
dominated by compile/JIT size; not a multi-× hot-loop speedup.**

---

## 9. Phase C (method DCE / accessors / dense for-in) — 2026-07-16

Implemented and verified with `sh cy-validate/run-validate.sh` → **52 passed,
0 failed, 0 crashed**.

| Item | What landed |
|------|-------------|
| **C1 method-level DCE** | Free-func seed → worklist with same-class name fill-in → **class-expand live monomorphs once** → post-expand worklist (no second expand). Pure method-level alone failed validate (incomplete graphs). Hybrid keeps unused monomorphs dead. |
| **C2 accessor inline** | Midopt sets `inline_p` on Count/Get/GetMut/IsEmpty/Capacity/KeyAt/ValAt/ValMut. `gen_class_method_call_dest` emits **`MIR_INLINE`** when `inline_p`. |
| **C3 dense for-in** | List/Set with `data`+`length` fields: for-in loads length/data once and uses `*(data+i)` — **no Count/Get calls**. Map still uses KeyAt/ValAt. |
| **Bugfix** | Detach sentinels `def_node = (node_t)1/2` must not be dereferenced in midopt (SEGV on val-021). |

Sketch list-sum after C: `kept=72 dead=66`, `funcs=81`, `insns≈2529`, `calls≈421` (similar to Phase B size; C2/C3 target **call density at runtime**, especially for-in).

---

## 10. Phase D — open-code dense accessors (2026-07-16)

| Item | Status |
|------|--------|
| **Open-code Count / IsEmpty / Capacity / scalar Get / scalar GetMut** | Landed in `try_open_code_dense_accessor` via `gen_class_method_call_dest` (protocol + for-in paths). |
| **N_CALL open-code intercept** | **Reverted** — double-eval of value receivers broke `GetMut().Boost()` chaining and nested `List<List>`. |
| **MIR_INLINE on Get/GetMut** | **Not used** — miscompiles pointer-into-buffer returns under chaining. Count/IsEmpty/Capacity still get `inline_p`. |
| **Class Get by-value** | Not open-coded (needs deep Copy for move-only nested collections). |

Validate: **52 / 0 / 0** after the above constraints.

Next runtime levers: safe N_CALL open-code (single receiver eval), open-code First/Last/FirstMut/LastMut for scalars, Map KeyAt/ValAt dense arrays, avoid specializing unused monomorphs.

---

## 11. Phase E — proven-safety guard elision (2026-07-16)

When the front-end can **prove** a fact, skip the corresponding `_safety_trap`.

| Proof | Elision |
|-------|---------|
| Stack value-class receiver (`TM_CLASS` → `&slot`) | `GEN_SAFE_SKIP_NULL` on open-code / protocol calls |
| One null check before for-in / seq loop | Subsequent Count/Get/KeyAt/ValAt: `SKIP_NULL` |
| Loop `i` with header `i >= n → break` | Get/KeyAt/ValAt / dense loads: `SKIP_OOB` |
| Dense Map for-in (`count`+`keys`+`vals`) | Direct `keys[i]`/`vals[i]` — no KeyAt/ValAt, no OOB |
| Dense List/Set for-in | Already field loads; no per-iter Get OOB |
| midopt: `this`, `&local`, `*(&x)` | `own_deref_class = SAFE` → gen skips null |

Flags: `GEN_SAFE_SKIP_NULL` / `GEN_SAFE_SKIP_OOB` on
`try_open_code_dense_accessor` / `gen_class_method_call_dest`.

**Still checked (not proven):**

- First/Last on possibly-empty collections  
- Get(i) with dynamic `i` outside a counted loop  
- Pointer receivers without a prior null check or SAFE stamp  
- Div/shift/OOB on C arrays without const-index proof  

Validate: **52 / 0 / 0**. list-sum MIR ~2526 insns / 420 calls (slight drop from
fewer null checks on stack `Count`/for-in).

---

## 12. Phase F — method-level midopt (no whole-class expand) (2026-07-16)

Replaced **whole-class expand** (kept every method of a live monomorph) with:

1. Free-func seed → worklist (same-class name fill-in)  
2. Gen protocol stamps: for-in (skip if dense open-code), `class[i]` Get/GetMut,
   `class[i]=` Set  
3. Helper fill on live monomorphs only: `EnsureCapacity`, `init_storage`,
   `grow_table`, `ensure_table`, `find_slot`, `find_index`, `destroy_*`, `Copy`,
   `Clear`, `owns*`  
4. Keep **all ctors/dtors** of any class that has ≥1 keep (delete/RAII paths)  
5. **No** whole-class public-API expand  

### list-sum impact (same machine)

| Config | funcs | insns | calls | `.bmir` | methods kept/dead |
|--------|------:|------:|------:|--------:|-------------------:|
| **Starting** (`-fno-midopt`) | 147 | ~4728 | ~860 | **27 975** | all |
| Phase B–E (class expand) | 81 | ~2529 | ~421 | ~18 6xx | 72 / 66 |
| **Phase F (method-level)** | **22** | **507** | **59** | **5 252** | **13 / 125** |

vs starting: **~6.7× fewer funcs**, **~9× fewer insns**, **~14× fewer calls**,
**~5.3× smaller BMIR**.

vs class-expand midopt: **~3.7× fewer funcs**, **~5× fewer insns**, **~7× fewer
calls**, **~3.5× smaller BMIR**.

Validate: **52 / 0 / 0**.

ST2000 ticks/sec still noise-flat vs `-fno-midopt` (hot path uses many live
methods). Compile/JIT graph size is the clear win.

---

## 13. Phase G — midopt safety lattice (nullness + intervals) (2026-07-16)

Per-function forward analysis in `midopt.c` (after method DCE):

| Domain | Facts | Use |
|--------|-------|-----|
| **Nullness** | TOP / NULL / NONNULL on locals (`int *p = NULL`, `p = &x`, `if (p==NULL)`) | Definite null deref → **warning** (or **error** with `-fsafety-errors`); NONNULL + `DEREF_GUARD_DEFAULT` → **SAFE** elision |
| **Intervals** | Known `[lo,hi]` for local ints / const exprs | Fixed C array `a[i]`: full interval in `[0,n)` → `elide_oob_p`; fully outside → definite OOB diagnostic |
| **Div / shift** | Exact 0 divisor; shift count fully out of width | Definite diagnostics |

**Conservative rules (correctness):**

- Do **not** stamp SAFE over ownership `DEREF_GUARD_CHECK` (object-guards UAF path).  
- Do **not** treat `new` as NONNULL for elision (MaybeOwned after conditional free).  
- Calls kill nullness/intervals on pointer/int args (may free or mutate).  
- Loop bodies kill all intervals after the loop.

**Flags:** `-fsafety-errors` promotes diagnostics to errors; default is warning
(so intentional trap demos still compile). Sketch: `sketch/sketch-midopt-safety.cy`.

**Still not (Level 2+):** full CFG SSA, heap shape, List.Get length coupling,
borrow checking.

---

## 14. Phase H — header quick harvest + cy-bench harness (2026-07-18)

From [`GEN-OPT-RESEARCH.md`](GEN-OPT-RESEARCH.md) R3/R4/R6.  Header-only
changes plus one midopt helper-name; no gen changes.

| Item | Where | Result |
|------|-------|--------|
| `Add` capacity fast path (`length >= capacity` guard before `EnsureCapacity` call) | `include/list.h` | removes a thunked call per non-growth Add |
| `insert_new_at(slot, k, v)` single-probe primitive factored out of `Set` | `include/map.h` | `Set` behavior identical (validate green) |
| `TryAdd` single-probe (was `Contains` + `Set` = 2) | `include/map.h` | **−16%** on 10k-op bench (median, interleaved A/B) |
| `GetOrAdd` single-probe on miss | `include/map.h` | neutral on hit-heavy bench (hit path was already 1 probe) |
| `GroupBy` (Map method + free `GroupBy`/`ListGroupBy`) single-probe (was `Contains` + `Set` + `GetMut` = 3) | `include/map.h` | **−32%** on 30×10k-elem bench |
| `insert_new_at` added to `midopt_helper_names` | `src/midopt.c` | DCE keeps it on live monomorphs |
| Bench harness `cy-bench/` (`bench-list-pipeline.cy`, `bench-map-probes.cy`, `run-bench.sh`) | new | checksums double as correctness tests |

**Reverted after measurement:** single-`Get` rewrite of `Where`/`Filter`/
`Distinct` (`T item = Get(i); … Add(move item)`).  It measured **~45%
slower**: the named local pays a per-iteration RAII dtor registration, and
prvalue `Get(i)` already binds straight into the `pred`/`Add` params with no
extra copy.  The real fix is borrow-don't-copy (R2, compiler-side).  The
`Filter` comment now records the *actual* reason for the double-Get.

**Pitfall found:** a monomorph of one free generic cannot call another free
generic's specialization (`ListGroupBy` → `GroupBy` = unresolved reference in
`__genfn_ListGroupBy_int_int`, val-032 SIGSEGV).  Keep duplicated bodies.

**Validation:** `cy-validate` **54 passed / 0 failed / 0 crashed**.
`bugs/run-bugs.sh` 8/3/0 — failures are the pre-existing stale expectations
(003, 004, 007) documented in `CLASSYC-FINDINGS.md` §3.

**A/B protocol:** interleaved runs (old headers stashed ↔ new headers) because
absolute times are load-sensitive; medians of 3 rounds reported above.

---

## 15. Phase I — R1 symbolic-constraint guard elimination (2026-07-18)

From [`GEN-OPT-RESEARCH.md`](GEN-OPT-RESEARCH.md) R1 (LLVM IRCE /
ConstraintElimination analog, at the typed-AST layer).

**What landed (`src/midopt.c`, `src/classyc.c`):**

| Piece | Where | Behavior |
|-------|-------|----------|
| IV recognition for `N_FOR` | `midopt_safety_for` | `for (i = LO; i < B; step++)` with non-decreasing step; interval `[LO, B-1]` set on the IV in the body env (constant B) |
| Symbolic bound `i < recv.Count()` | `struct midopt_fact.sym_recv/sym_lo` | direct form and via bound local `int n = recv.Count();` |
| Hazard analysis | `midopt_iv_hazard_p`, `addr_taken_p`, `midopt_kill_sym_for`, safe-method whitelist | shrink/unknown method on recv, `&recv`, `move recv`, recv/IV/bound writes, escaped receiver → no elision (growth is SAFE: `Add` etc. whitelisted) |
| Stamp consumption | `elide_oob_p` on N_CALL/N_IND | gen N_CALL intercept and class subscript add `GEN_SAFE_SKIP_OOB`; C arrays already consumed it |
| N_CALL open-code enabled | gen intercept | resolves accessor by **name+arity** when `def_node` absent (monomorph paths) |

**Measurements:**

| Bench | Before | After | Speedup |
|-------|-------:|------:|--------:|
| `for(i<xs.Count()) s+=xs.Get(i)` (cy-bench/bench-iv-access) | 135.2 ms | 39.6 ms | **3.4×** |
| `xs[i]` same loop | 110.4 ms | 42.0 ms | **2.6×** |
| C array `a[i]` counted loop | 4.2 ms | 1.8 ms | **2.3×** |
| sketch-midopt-iv-guards traps in main | 6 sites guarded | 1 (intentional hazard) | — |
| space-trader ST2000 `-O2 -fno-exceptions` ticks/sec (median of 3) | 48813 | 51945 | +6% (noise-adjacent) |

**Pitfalls discovered (worth remembering):**

1. **Check injects the receiver as `args[0]` of every non-static method call**
   (`N_ADDR(obj)` for value receivers, pointer copy for `->`).  Everything that
   inspects method-call args must skip it: the gen N_CALL intercept computed
   arity wrong (never fired), midopt's `N_ADDR` walk marked every receiver
   addr-taken (poisoned escape proofs).  Both fixed with skip-receiver logic.
2. **Locals link via `expr.u.lvalue_node` (the N_SPEC_DECL), not `def_node`**
   (NULL for plain local uses).  `midopt_id_decl` now falls back — this also
   repairs the *existing* Phase G lattice, whose local tracking silently
   matched nothing through `def_node` alone.
3. `midopt_same_decl(NULL, NULL) == 1` foot-gun: null-check ordering in
   identity comparisons.
4. Open-coded accessor OOB traps report the *method's* source line (list.h:218)
   not the call site's — trap line numbers are not per-site distinguishable.

**Correctness:** `cy-validate` **54/0/0**; bugs 8/3/0 (pre-existing stale);
sketch `sketch-midopt-iv-guards.cy` PASS (guard-free results identical);
aurora-ops / neon-grid run clean.

**Scope notes (v1):** value-class receivers only (pointer receivers alias);
`while`-form IVs not yet recognized; `i <= n` only for constant bounds;
user-defined dense classes need whitelisted method names to elide.

---

## 16. Phase J — R2 borrow-don't-copy (2026-07-18)

From [`GEN-OPT-RESEARCH.md`](GEN-OPT-RESEARCH.md) R2 (C++ NRVO / LLVM SROA /
GCC ipa-sra analog, adapted to ClassyC's typed-AST midopt + gen).

### What landed

| Piece | Where | Behavior |
|-------|-------|----------|
| `is_move_only<T>()` intrinsic | classyc.c (parse special-form, specialize substitution, check fold) | folds to 1 for List/Map/Set instantiations, 0 else — lets generic bodies pick a Copy-safe path per element kind |
| R2.2b zero-copy HOFs | `include/list.h` | Filter/Where/CountWhere/Any/All/Find/FindOr/ForEach/Map/Select/SelectString/Distinct/Copy/Equals/Slice/Concat/AddRange/InsertRange/Take/Skip/ToArray/CopyTo: non-move-only elements read straight from `data+i` (pred/Add params still copy — semantics identical); move-only keeps the Get prvalue-Copy path |
| R2.1 for-in by-ref | `src/midopt.c` proof + `src/classyc.c` gen | `for (auto s in xs)` over dense List/Set class elements: when the body provably never mutates the var or the collection and calls only proven read-only methods on the var, gen binds the var as a **pointer into the buffer** instead of a per-iteration block copy |
| R2.2a capturing-lambda deref | `src/classyc.c` open-code builder | capturing Where/CountWhere/Find/Any/All/Map/Select: param substituted by `*(recv.GetMut(i))` instead of `recv.Get(i)` when the param is read-only and the element is a non-move-only aggregate |

### Measurements

| Bench | Before | After | Speedup |
|-------|-------:|------:|--------:|
| for-in over 2000×64B elements ×3000 (midopt on vs `-fno-midopt`) | 80 ms | 27 ms | **2.9×** |
| CountWhere 2000×64B ×4000 (header stash A/B) | 510 ms | 334 ms | **1.5×** |
| Where same (A/B) | 102 ms | 81 ms | **1.25×** |
| capturing CountWhere MIR | 2 Get calls + 2 block copies/elem | 2 GetMut, **0 copies** | — |

### Mechanism notes (for future work)

- **Proofs are the product.** R2.1's conditions: loop var never assigned /
  INC / moved / address-taken; collection never mutated (pure-read method
  whitelist — growth is NOT allowed here since a realloc moves the buffer);
  method calls on the var must pass a depth-capped `this`-write body analysis.
  All failures fall back to the copy path with identical semantics.
- For-in loop vars carry **no RAII dtor** today (verified empirically) — the
  by-ref binding changes nothing about destruction (the List owns elements).
- `is_move_only<T>()` is name-based (mangled `__generic_{List,Map,Set}_`),
  mirroring `class_is_move_only_collection_p`.  `List<int>*` is 0 (pointers
  copy bitwise).  `>>` in nested type args needs `c2m_pending_extra_gt`
  handling in the parse special form (is_pointer has the same latent gap).
- Call-arg init from an lvalue does **not** get the move-only Copy rewrite
  (only N_RETURN / N_ASSIGN do) — `Add(*p)` on `List<List<int>>` shallow-copies
  and double-frees.  This is why move-only T keeps the Get prvalue path.
- Method calls inject the receiver as args[0] — any AST use-walk must skip it
  (same lesson as R1).
- For-in var N_IDs are declaration sites: no `u.lvalue_node`; resolve via
  `symbol_find (S_REGULAR, var, forin_node)`.

### Validation

`cy-validate` **54/0/0**; bugs 8/3/0 (pre-existing); probes:
`sketch/probe-forin-byref.cy` (byref + 3 hazard fallbacks, all modes),
`sketch/probe-hof-deref.cy` (capturing Where/CountWhere/Find + mutating-lambda
fallback + move-only nested), `/tmp/test-ismo*.cy` (intrinsic matrix).
aurora-ops / neon-grid clean.

### Deferred within R2

- Sort comparator params (open-coded Sort's tmp move is required; comparator
  deref is possible but the shift logic makes it delicate).
- R2.3 by-value class function params → pointer (borrow params; ABI-visible,
  ownership `PA_BORROWS` inference input).
- Map dense for-in by-ref (values arrays), pointer-receiver collections.

### R2.1 addendum — Map for-in by-ref (2026-07-18, same day)

Extended the for-in borrow to dense Maps (`keys`+`vals`+`count`): the two-var
`for (auto k, v in m)` value var binds by reference when `V` is a class and
the body is read-only over an unmutated map (Map read methods added to the
pure whitelist; `Set`/`TryAdd`/`insert_new_at` etc. disqualify).  Keys stay
copied (scalars/Strings are single loads).  Probe:
`sketch/probe-map-forin-byref.cy` (byref + 3 hazard fallbacks).  Bench
(1500×72B values ×4000 iterations, midopt on vs off): **100 ms → 36.7 ms
(2.7×)**.  cy-validate 54/0/0.
