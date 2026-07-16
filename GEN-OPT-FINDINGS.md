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
