# GEN-OPT — Path to LLVM-class efficiency

Last updated: 2026-07-16

Scope: MIR SSA optimizer (`ext/mir/mir-gen.c`, `mir.c`), ClassyC MIR generation
(`src/classyc.c` `gen_*`), and a **mid-level optimizer between check and gen** —
the highest-leverage place for ClassyC-shaped opts that MIR will never see.

Related: [`CLASSYC-FINDINGS.md`](CLASSYC-FINDINGS.md), [`BY-VALUE.md`](BY-VALUE.md),
[`LAMBDA-CAPTURE.md`](LAMBDA-CAPTURE.md), MIR README §“MIR JIT compiler”.

---

## 0. Executive summary

| Layer | Today | Gap vs LLVM `-O2`/`-O3` | Best fix locus |
|-------|--------|-------------------------|----------------|
| **Front-end MIR quality** | Expression-tree gen + safety/arena calls | Fat IR: method CALL spam, always-on guards, String/dict runtime | **`classyc.c` gen + mid-opt** |
| **Mid-level (check → gen)** | `ownership_run` only | No CFG DCE, no dead-method prune, no call specialization | **New `midopt` phase** |
| **MIR SSA (`-O2`)** | GVN/CCP, copy-prop, DSE, DCE, LICM, pressure relief, combine, linear-scan RA | No unroll/vectorize/SR; mem-GVN needs `-O3`; conservative call clobber | **`mir-gen.c` selective deepen** |
| **MIR link** | Simplify + `MIR_INLINE` only if caller marked `inline` | Most class methods stay real calls (thunk path) | Headers + mid-opt marks |
| **AOT** | Full monomorph graph in `.bmir` | ~90% unused `List` methods measured | DCE / `used_p` / sections |

**Product stance:** Do **not** try to become LLVM. Keep MIR’s compile-time
advantage (c2m ~100× faster compile than gcc, ~91% of gcc `-O2` on small C
benches). Close the gap where ClassyC **loses** that headroom: collections,
safety, monomorph bloat, and call density.

**Realistic targets (ClassyC apps, not pure sieve):**

| Workload | Today (est.) | Near-term goal | Stretch |
|----------|--------------|----------------|---------|
| Scalar numeric loops (`-fno-exceptions`) | ~0.85–0.95× gcc `-O2` | ≥0.95× | match |
| `List<int>` / `Map` pipelines | much slower (CALL + bounds + alloc) | **2–5×** fewer calls/allocs | competitive with hand C |
| AOT binary size (hello + List) | huge unused methods | **5–10×** smaller | section GC |
| Compile time (self-host classyc) | ~6.8s (own 36% / gen 42%) | midopt free or tiny; own faster | gen not regress |

---

## 1. Pipeline map (where work can land)

```
pre → parse → do_context (check) → ownership_run → [MIDOPT] → gen_mir
       → MIR_finish_module → MIR_link (simplify + process_inlines)
       → MIR_gen (SSA / RA / combine / machine)
       → JIT (-eg/-el/-eb)  or  b2obj → ld (AOT)
```

Timing hooks already exist in `c2mir_compile` (`init/preprocess/parse/check/
ownership/generate`). Add a `midopt` stage next to ownership.

### 1.1 What each stage is good at

| Stage | Strengths | Weaknesses |
|-------|-----------|------------|
| **check** | Types, const fold (`expr.const_p`), monomorph, HOF open-code, rewrite sugar | One-pass; not a CFG; side effects of lowering already baked in |
| **ownership** | Lattice over `new`/`malloc`/`owned`; elides some null guards via attributes | Not a general optimizer; expensive (~36% self-compile) |
| **midopt (new)** | Full typed AST + scopes; ClassyC-aware; cheap rewrites before any MIR | Must preserve defer/arena/exception semantics |
| **gen** | Emit MIR; peephole `emit_insn_opt`; safety/arena injection | Sees one node at a time; hard to DCE whole methods |
| **MIR -O2** | Fast SSA, good for scalar C | No vec/unroll; calls kill mem-avail; thunk calls |
| **MIR -O3** | Load/store GVN + DSE depth | Still no IPA beyond inline; still no loops beyond LICM |

---

## 2. MIR SSA optimizer — inventory

Source of truth: `generate_func_code` in `ext/mir/mir-gen.c` (~9555+).

### 2.1 Pass pipeline (`optimize_level`)

| Level | Behavior |
|-------|----------|
| **0** | Fast gen: CFG + machinize + simplified RA |
| **1** | + loops, combine, DCE after combine |
| **2** (default) | + BB clone, **build SSA**, GVN/CCP, copy-prop, DSE, SSA DCE, **LICM**, pressure relief, conventional SSA + **ssa_combine**, out-of-SSA, jump opts, coalesce, linear-scan RA, combine |
| **≥3** | GVN **load/store elimination** depth (`optimize_level < 3` skips most mem cases) |

**Pipeline order (-O2):**

1. Build CFG → (clone BBs)  
2. Build SSA (Braun-style) → optional ADDR transform + rebuild SSA  
3. **GVN** (incl. CCP; mem availability; **calls clear all mem_avail**)  
4. **Copy propagation** + redundant ext removal  
5. **DSE**  
6. **SSA DCE**  
7. **LICM** (pressure-sensitive)  
8. **Pressure relief**  
9. Conventional SSA → **ssa_combine** (addr fold, cmp+branch) → undo SSA  
10. **Jump opts**  
11. **Machinize** + 2-op form  
12. **Coalesce** → **RA** (priority linear scan + split)  
13. **Combine** (code selection) → DCE  
14. Prolog/epilog → machine emit  

Inlining is **not** in `MIR_gen`: it runs at **`MIR_link`** via `process_inlines`
for `MIR_INLINE` call ops only. ClassyC emits `MIR_INLINE` only when the callee
decl has `inline` (`classyc.c` ~29460).

### 2.2 What MIR already does well

- Compile speed orders of magnitude above LLVM for small funcs.  
- Solid SSA infrastructure: phi, SSA edges, rename, conventional-SSA exit.  
- GVN + CCP + LICM enough for sieve/nbody-class C (~0.91 geomean vs gcc `-O2`).  
- Aggressive coalesce + combine recovers two-address x86 forms.  
- Optional alias/nonalias on mem ops; ClassyC already emits `MIR_new_alias_mem_op`
  via `get_type_alias` for structs/classes.  
- Lazy BB versioning (`-eb`) and lazy funcs (`-el`) for JIT iteration.

### 2.3 MIR gaps vs LLVM (honest ranking)

**High impact / feasible in MIR**

1. **Default mem-GVN at -O2 for stack/alloca “must” memory** — today full
   load/store elim is gated behind `optimize_level >= 3`. ClassyC frames are
   mostly `must alloca`. Enabling a *restricted* mem-GVN at -O2 for
   non-escaping frame slots is closer to LLVM mem2reg + local CSE without full
   AA.  
2. **Call purity / effects** — any `MIR_CALL` clears `curr_available_mem`.
   Marking pure runtime helpers (`c2m_str_length`, `memcmp`-like, pure math)
   and readonly methods would unlock LICM/GVN across calls.  
3. **Richer inlining heuristic** — size budget, hotness, always-inline for
   one-line accessors (`Get`/`Count`/`IsEmpty`). Today: all-or-nothing
   `inline` keyword.  
4. **Loop opts beyond LICM** — strength reduction, simple unroll of
   counted loops with const trip count, IV widen.  
5. **AOT DCE** in `b2obj` / MIR module (reachability from roots).

**Medium / hard**

6. Graph-coloring or PBQP RA (diminishing returns vs compile time).  
7. Better scheduling / store-load forwarding on x86.  
8. Interprocedural constant prop without full LTO.

**Out of scope / wrong product bet**

9. Auto-vectorization (AVX) — huge LLVM surface; use explicit SIMD later.  
10. Whole-program LTO like LLVM — optional later via multi-module MIR.

### 2.4 Known MIR correctness constraint

`cycle_phi_p` (bugs/001): address combining must not fold across multi-block
phis. Any new MIR combine/LICM must keep this barrier. See `bugs/001-NOTES.md`.

### 2.5 MIR design tax: call thunks

From MIR c-benchmarks notes: call-heavy codes pay for **thunked** calls so
code can be hot-swapped. Direct calls would help `method-call` / List
pipelines but fight JIT design. Mitigations:

- Prefer **inline** / open-code for tiny methods (midopt + headers).  
- JIT: already have lazy gen; keep hot List paths inlined.  
- AOT: could emit direct relocs for non-exported monomorphs (future).

---

## 3. ClassyC gen (`src/classyc.c`) — IR quality audit

### 3.1 Emission style

- Treewalk `gen` / `val_gen` → `emit2` / `emit3` / `emit_insn_opt`.  
- Front-end peephole only: temp copy forwarding; branch-to-label cleanup.  
- No function-level MIR cleanup before `MIR_finish_func`.  
- Frame model: locals as mem/regs; class BLK params; call-arg area for stmtexpr
  class results (see FINDINGS).

### 3.2 Runtime call surface (primary speed tax)

| Family | Examples | Cost |
|--------|----------|------|
| **String** | `c2m_str_*`, concat, arena checkpoint/release | call per op + loop scope marks |
| **dict** | create/get/set/serialize | call + heap |
| **Safety** | `_safety_trap` guards (null/div/oob/shift) | branch + cold call per site (`exceptions_p` default **on**) |
| **Collections** | monomorph `List`/`Map` methods as normal MIR_CALL | no auto-inline unless `inline` |
| **Exceptions** | `setjmp` / `cy_exc_*` | frame + call on try paths |
| **Ownership runtime** | `cy_safe_*`, obj arena | when enabled |

Safety guards are correct-by-default product features but they:

- split basic blocks (hurt GVN/LICM),  
- add cold calls MIR will not prove dead without purity + range facts,  
- fire on every index/deref unless ownership stamped SAFE.

### 3.3 What gen already does right

- `get_type_alias` → MIR alias tags for field access.  
- Capturing HOF open-code (Strategy A) → no closure CALL.  
- Dense nested List buffer: `*(data+i)` + memcpy growth (SSA-friendly).  
- Scalar `move` pass-through; stack value collections.  
- Optional elision of null checks when ownership proves SAFE.

### 3.4 Gen-level quick wins (no midopt)

1. **Hot path `-O` profile:** document/ship  
   `classyc -O2 -fno-exceptions` (or `-fno-safety-guards`) for release benches.  
2. **Mark pure imports** when MIR grows purity (or local gen annotation).  
3. **`inline` on List/Map/Set micro-methods** in headers: `Count`, `IsEmpty`,
   `Get`/`Set` for scalars, `Contains` thin wrappers — emit as `MIR_INLINE`.  
4. **Fold const trip / const index** already partially via `const_p`; extend
   gen to skip OOB when `const_p && 0 <= i < n`.  
5. **Batch String arena** work: avoid checkpoint/release pairs in empty loops
   (already conservative when outer assign; tighten dead empty scopes).  
6. **Don’t emit redundant force_reg MOV chains** — widen `emit_insn_opt`.

---

## 4. Mid-level optimizer (check → gen) — **recommended main bet**

### 4.1 Why here beats more MIR passes

ClassyC IR is not “C with sugar.” After check, the AST still has:

- monomorphized class methods and unused siblings,  
- `for-in` / value List pipelines as method calls,  
- ownership lattice results,  
- `defer` / arena checkpoints as structural nodes,  
- exception try regions.

MIR only sees lowered CALL/MOV/branch soup. **Classic mid-end opts on the typed
AST (or a thin CFG of AST blocks) are easier, safer, and ClassyC-specific.**

Precedent already in-tree:

| Mechanism | Phase | Pattern to copy |
|-----------|-------|-----------------|
| Capturing HOF open-code | check | call → loop AST |
| `arr.ToList()` rewrite | check | sugar → ctor |
| `is_pointer<T>()` fold | check | dead branch for generics |
| `ownership_run` | post-check | whole-module walk + attributes for gen |
| Lambda → static func | check | monomorph + inject |

**Placement:** after `ownership_run`, before `gen_mir` (same `c2mir_compile`
branch). Share ownership’s CFG/candidate caches if possible (FINDINGS §4).

### 4.2 Midopt design sketch

```
// src/midopt.c  (included like ownership.c, or separate TU with shared headers)

void midopt_run(c2m_ctx_t, node_t module);   // -O0: no-op; -O1+: below

// Optional internal IR:
//   mid_func { node_t fdef; mid_bb *cfg; use-def on decls }
// Prefer AST rewrite first; only build CFG when needed (DCE, const prop).
```

**Flags (proposal):**

| Flag | Effect |
|------|--------|
| `-O0` | midopt off (debug) |
| `-O1` | cheap: dead method prune marks, obvious const if/while, pure call marks |
| `-O2` (default with MIR -O2) | + safety elision using ownership, inline marks, simple DCE |
| `-O3` / `-fmidopt-aggressive` | + method specialization, for-in lowering, limited unrolling marks |
| `-fno-midopt` | skip (parity with `-fno-ownership`) |

### 4.3 Pass list (classic compiler stages, ClassyC-shaped)

Ordered by **ROI / risk**. Each pass rewrites AST or stamps `decl`/`expr` attrs
that gen already (or will) consult.

#### P0 — Dead monomorph / used-method analysis  **[AOT + compile]**

**Problem:** Instantiating `List<int>` pulls ~30 methods into the module; hello
world only needs ctor/Add/dtor. Measured ~90% dead (FINDINGS §5).

**Algorithm:**

1. Roots: `main`, exports, addresses taken, vtables/`Any` factories, reflection.  
2. Call graph over monomorph methods + free funcs (AST N_CALL / method resolve).  
3. Mark `used_p` on `decl_t` / func items.  
4. **Skip gen** for unmarked method bodies (emit forward or nothing).  
5. AOT: optional second pass at BMIR write / b2obj reachability.

**Why midopt not MIR:** MIR never sees unreferenced monomorphs if gen never
emits them — cheapest DCE possible.

#### P1 — Safety / bounds / null elision  **[runtime]**

**Problem:** Default exceptions inject guards that MIR cannot remove.

**Facts available post-ownership:**

- `DEREF_GUARD_SAFE` / MaybeOwned / null lattice.  
- `const_p` index and known array/list lengths.  
- Non-null receivers: `&obj`, `new T`, stack address.

**Actions:** stamp `expr->no_null_check_p` / `no_oob_check_p` / gen consults
(partially exists). Midopt can **propagate** more aggressively:

- after `if (p)` dominated uses,  
- loop induction `i` with `0..Count()` for-in,  
- `Get` after `Count` compare.

Also: global `-frelease` = `-fno-exceptions` + midopt elision for benches.

#### P2 — Accessor inlining marks  **[runtime]**

For monomorph methods matching patterns:

```text
Count()      → return this->length;
IsEmpty()    → return this->length == 0;
Get(i)       → return this->data[i];   // still may need OOB if safety on
GetMut(i)    → return &this->data[i];
```

Stamp `always_inline_p` / force `decl_spec.inline_p` so gen emits `MIR_INLINE`.
Alternatively **AST-inline** into caller (copy body with `this` subst) for
1–3 insn methods — avoids MIR inline size limits and thunk tax.

Capturing HOF already open-codes; extend open-code table for:

- non-capturing `Where`/`Select` with tiny predicates (optional),  
- `for (auto x in list)` → index loop over `data`/`length` without virtual Count CALL.

#### P3 — Classic local opts on AST  **[runtime + IR size]**

| Pass | Notes |
|------|--------|
| **Const prop / fold** | Extend check’s `const_p` across statements: `int n = 3; … n` in same block if `n` never assigned. SSA-lite on scalar locals. |
| **Dead store / dead local** | Local never read → drop decl (careful with dtors / String arena). |
| **Dead code** | `if (0)`, `while(0)`, `if (const)` after monomorph `is_pointer` already; finish after const prop. |
| **Branch simplification** | Same as emit_label_insn_opt but on AST. |
| **Copy prop** | `x = y; use x` → `use y` for scalars/pointers without intervening write. |
| **Common subexpr (local)** | Dominator-local only; don’t fight side effects. |

These are textbook **Dragon Book** local/global opts; do them **before gen** so
MIR receives less junk (faster MIR_gen too).

#### P4 — Strength reduction & loop shapes  **[runtime]**

ClassyC idioms:

```c
for (int i = 0; i < xs.Count(); i++) { ... xs.Get(i) ... }
for (auto x in xs) { ... }
```

Rewrite to:

```c
int __n = xs.length;   // or one Count()
T* __p = xs.data;
for (int i = 0; i < __n; i++) { T x = __p[i]; ... }
```

With safety: one OOB-free loop after proving `Get` range.  
With `-fexceptions`: keep a single bounds assert at entry optional.

Also: `x * 2` → `x << 1` only when signed-safe / unsigned; optional.

#### P5 — Call specialization / devirtualization  **[runtime]**

- `Any<I>` with known concrete factory → direct method.  
- `Map`/`List` method calls where receiver type is exact monomorph (always) →
  already direct; focus on **inline**.  
- Dict path stays dynamic (runtime tags) — only specialize typed JSON bind
  sites already lowered in check.

#### P6 — Arena / defer simplification  **[runtime]**

- Empty scope with checkpoint/release and no String-producing calls → drop
  checkpoint pair.  
- Function with no heap String → no function-level String arena frame.  
- `defer` of no-op after DCE → drop.

Must respect exception paths (release on all exits) — ownership already
understands multi-exit; midopt should reuse that CFG.

#### P7 — Escape analysis lite  **[runtime / alloc]**

- `List` built and only iterated, never escaped → stack buffer or small-size
  SBO (future ABI).  
- `new T` immediately consumed by `owned` local and never escapes → already
  managed; optional stack allocate POD-like classes (hard with reentrancy).

Phase this after P0–P4.

### 4.4 Interaction with ownership

| Ownership | Midopt |
|-----------|--------|
| Builds CFG / lattices | **Reuse** — don’t rebuild thrice (FINDINGS: ownership 36%) |
| Stamps SAFE/CHECK | Midopt **consumes** for P1 |
| Synthesizes defer delete | Midopt must run **after** synthesis so DCE sees real defers |
| `-fauto-release` | Midopt DCE must not drop synthesized cleanup |

Suggested internal order:

```
check → ownership_run → midopt_run → gen_mir
```

### 4.5 What **not** to put in midopt

- Full SSA for all MIR-level arithmetic (duplicate MIR GVN).  
- Vectorization.  
- Register allocation.  
- Cross-module LTO (later, at MIR/b2obj).

Midopt should **lower ClassyC → better C-shaped AST**, then let MIR do scalar SSA.

---

## 5. AOT / size / link

1. **P0 used_p** (above) — first priority.  
2. **b2obj reachability DCE** — delete unreferenced MIR funcs before ELF.  
3. **Per-function sections + `--gc-sections`** when linking with gcc.  
4. **Lazy JIT `-el`** remains best *runtime compile* default; AOT needs real DCE.  
5. Optional **MIR `-O3`** for AOT release builds only (mem-GVN).

---

## 6. Measurement plan (gate every PR)

### 6.1 Correctness

- `sh cy-validate/run-validate.sh` (52 vals)  
- `sh bugs/run-bugs.sh` (esp. 001 at -O2/-O3, 009 shift)  
- `examples/run-examples.sh`  
- midopt: golden dump of AST or MIR for a few fixtures under `-fdump-midopt`

### 6.2 Performance suites

| Suite | What it measures |
|-------|------------------|
| `ext/mir/c-benchmarks` | baseline MIR vs gcc (no ClassyC) |
| New `cy-bench/` | List/Map pipelines, String, dict JSON, aurora-ops style |
| AOT size | `classyc -c` + `b2obj` + `size` / `nm` on hello+List |
| Compile | `-v` timings: ownership + midopt + generate |

### 6.3 Success metrics

| Metric | Target (12 months) |
|--------|--------------------|
| `List<int>` sum loop | ≥3× faster than current default-exceptions JIT |
| AOT .o size hello+List | ≥5× smaller |
| cy-validate | still green at `-O2` midopt |
| Self-compile | midopt ≤5% of total; ownership not worse |

---

## 7. Phased roadmap

### Phase A — Instrument & baseline (1–2 weeks) — **done 2026-07-16**

- [x] Sketch benches: `sketch/sketch-midopt-list-sum.cy`, `sketch-midopt-oob-elide.cy`, `run-midopt-bench.sh`.  
- [x] `-fdump-mir-stats` (funcs / insns / calls after gen).  
- [x] Release flags documented in [`GEN-OPT-FINDINGS.md`](GEN-OPT-FINDINGS.md).  
- [x] `-O` → `MIR_gen_set_optimize_level` confirmed in driver.

### Phase B — Midopt P0 + P1 (2–4 weeks)  **highest leverage** — **done 2026-07-16**

- [x] `src/midopt.c` (include model like `ownership.c`) + `-fno-midopt` + `midopt=` timing.  
- [x] Dead class-method prune (`midopt_dead_p`); free-func seed + for-in protocol + class expand.  
- [x] P1: const array OOB elision (`elide_oob_p`); `this` → SAFE.  
- [x] AOT size win on List sketch (~1.5× smaller BMIR; ~2× fewer MIR funcs). Details: **GEN-OPT-FINDINGS.md**.

### Phase C — Inline accessors + for-in lower (2–3 weeks)

- [ ] `inline` / AST-inline Count/Get/GetMut/IsEmpty for List/Map/Set.  
- [ ] for-in → pointer walk for monomorph List/Set dense storage.  
- [ ] Measure List pipeline speedup.

### Phase D — Local classic opts (2–3 weeks)

- [ ] Block-local const prop + DCE + dead store (dtor-aware).  
- [ ] Empty String arena frame elision.  
- [ ] Optional: simple CSE.

### Phase E — MIR deepen (opportunistic)

- [ ] -O2 restricted mem-GVN for must-alloca frame slots.  
- [ ] Call purity flags for `c2m_str_length` / hash helpers.  
- [ ] Inline heuristic by size (not only keyword).  
- [ ] Counted-loop unroll ×2/×4 when trip const and body tiny.  
- [ ] b2obj reachability DCE.

### Phase F — Stretch

- [ ] Escape analysis / SBO for small Lists.  
- [ ] AOT direct calls for internal monomorphs.  
- [ ] Limited IPA across one TU (already single module).

---

## 8. Worked examples (what “good” looks like)

### 8.1 List sum

**Source:**

```c
auto xs = List<int>();
// ... fill ...
int s = 0;
for (auto v in xs) s += v;
```

**Today (conceptually):** Count/Get or for-in helper CALLs; OOB; possible arena noise.

**After midopt P2+P4:**

```c
// rewritten AST before gen
int __n = xs.length;
int *__p = xs.data;
for (int i = 0; i < __n; i++) s += __p[i];
```

MIR sees a tight ADD loop → LICM/RA win; matches hand C.

### 8.2 Pipeline

```c
auto evens = xs.Where((int x) => x % 2 == 0);
auto top = evens.Take(10);
```

Capturing already open-codes Where. Midopt should:

- keep open-code,  
- ensure Take/Where monomorphs that remain are **inline** or open-coded,  
- DCE unused List methods from the TU.

### 8.3 Safe vs fast

```bash
classyc app.cy -eg                    # product default: safety on
classyc app.cy -O2 -fno-exceptions -eg   # speed profile
classyc -c -O3 app.cy -o app.bmir && b2obj …  # AOT max
```

---

## 9. Risk register

| Risk | Mitigation |
|------|------------|
| Midopt breaks defer/arena | Run after ownership; tests with String loops + exceptions |
| Dead-method DCE drops needed method | Conservative roots: address-taken, `Any`, exported, `#pragma` keep |
| Inline explosion | Size cap; only ≤N MIR insns estimated |
| MIR -O3 miscompile (phi/addr) | Keep cycle_phi barrier; bugs/001 in CI at -O2 and -O3 |
| Ownership+midopt compile time | Shared CFG; midopt O(n) local first |
| Semantic change of OOB elision | Only when proved; flag to force guards |

---

## 10. Recommendation (what to build first)

1. **Midopt P0 (dead monomorph skip)** — size + compile; low semantic risk.  
2. **Midopt P1 (safety elision) + documented release flags** — runtime.  
3. **Accessor inline / for-in lower** — List-heavy apps.  
4. **Local const prop/DCE** — classic stage, cleans gen input.  
5. **MIR purity + restricted mem-GVN** — when midopt plateaus.  
6. **b2obj DCE** — AOT ship size.

The mid-level window between **ownership** and **gen** is the right place for a
classic optimizer *for ClassyC*: types, monomorphs, and ownership facts are
available, MIR is still free of ClassyC vocabulary, and every AST-level win
multiplies MIR’s already-strong scalar SSA backend.

---

## 11. File / code touch map (implementation guide)

| Piece | Location |
|-------|----------|
| Midopt driver | `src/midopt.c` include from `classyc.c` (mirror `ownership.c`) |
| Hook | `c2mir_compile` after `ownership_run`, before `gen_mir` |
| Options | `struct c2mir_options`: `midopt_p`, level; driver `-O*`, `-fno-midopt` |
| used_p | `decl_t` or side table; gen `N_FUNC_DEF` early-out |
| Guard elision attrs | extend existing ownership fields on `struct expr` |
| List open-code | check-time or midopt rewrite of for-in / Get loops |
| Headers | `include/list.h` `inline` on micro-methods |
| MIR purity | `ext/mir` insn/call attrs (design needed) |
| Mem-GVN -O2 subset | `mir-gen.c` `gvn_modify` store/load cases |
| AOT DCE | `src/b2obj.c` + optional MIR module walk |
| Benches | `cy-bench/`, extend `ext/mir/perf-tests` |

---

## 12. Open questions

1. Should default `-O2` enable midopt while keeping safety, or only size DCE?  
   **Proposal:** P0 always-on when optimizing; P1 elision only with proof or
   `-fno-exceptions`.  
2. AST-inline vs MIR_INLINE for accessors?  
   **Proposal:** AST-inline for ≤3 statements; else `inline` + MIR.  
3. Share ownership CFG construction with midopt in one `analysis_run`?  
   **Proposal:** yes in Phase B refactor once P0 lands.  
4. Push mem-GVN to default -O2 upstream MIR or ClassyC fork only?  
   **Proposal:** ClassyC fork first; upstream when benches prove compile cost.

---

*Built on MIR’s design: short pipeline, SSA where it pays, simplicity over
extreme peak codegen. ClassyC should win by **not lowering junk** and by a
**typed midopt** that LLVM-style backends never get for free.*
