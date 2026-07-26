# GEN-OPT-RESEARCH — Next-generation generics optimizations (LLVM/GCC-informed)

Last updated: 2026-07-18

Research phase for the next round of generics optimization, building on
[`GEN-OPT.md`](GEN-OPT.md) (plan) and [`GEN-OPT-FINDINGS.md`](GEN-OPT-FINDINGS.md)
(landed phases A–G). Sources: audit of `src/midopt.c`, `src/ownership.c`,
`include/{list,map,set}.h`, and the hot idioms in
`examples/classy-aurora-ops.cy`, `examples/classy-neon-grid.cy`,
`examples/spaceway3k/spaceway3k.cy`, `examples/beyond-demo/classyc-db-engine.h`;
cross-referenced against LLVM and GCC optimizer practice.

---

## 0. Baseline (what is already in tree)

| Piece | Status |
|-------|--------|
| Method-level dead monomorph prune (P0) | landed (midopt; list-sum: 147→22 funcs) |
| Dense open-code accessors + dense for-in (List/Set/Map) | landed (gen) |
| P1 const-index OOB elision; `this`/`&local` null elision | landed |
| Phase G safety lattice (nullness + **constant** intervals) | landed |
| Capturing HOF open-code (Where/Find/Sort/Select/…) | landed (check) |
| `inline_p` marks on Count/IsEmpty/Capacity | landed |
| Ownership CFG + 5-state lattice + IPA summaries | landed — **facts discarded after each function** |
| MIR -O2 SSA (GVN/CCP/copy-prop/DSE/LICM/combine) | upstream MIR; no vec/unroll/purity |

**The gap that matters:** ClassyC hot loops still pay (a) per-element by-value
struct copies, (b) per-element bounds/null guards that block inlining, (c)
re-hashing and re-probing for the same key, (d) calls in loop headers that MIR
cannot hoist. These are exactly the costs LLVM/GCC erase with passes we can
adapt at the AST/midopt level — where ClassyC type information is still intact.

---

## 1. Ranked optimization list

Rank = expected runtime impact on generics-heavy ClassyC code ÷ effort/risk.
Per-item: LLVM/GCC analog → ClassyC mechanism → locus → impact → risk.

### R1 — Symbolic-constraint guard elimination in loops  ★ top pick

**STATUS: landed 2026-07-18 (Phase I).**  Measured 3.4× on `xs.Get(i)` counted
loops, 2.6× on `xs[i]`, 2.3× on C-array loops (cy-bench/bench-iv-access.cy).
See GEN-OPT-FINDINGS.md §15.  Scope of v1: value-class receivers, `N_FOR`
loops; `while`-form IVs future work.

**LLVM analog:** `InductiveRangeCheckElimination` (IRCE) and
`ConstraintElimination` — the same passes Clang's `-fbounds-safety` relies on
to make inserted runtime checks nearly free. **GCC analog:** Ranger-based VRP.
Key design point: constraints are **symbolic** (`i < length`), not constant
intervals.

**Problem today:** `midopt.c`'s Phase G lattice tracks *constant* intervals
(`i ∈ [lo,hi]`). It cannot prove `i < xs.Count()` because `length` is not a
constant. So every `Get(i)` / `GetMut(i)` / `a[i]` inside a perfectly safe
counted loop keeps its throw guard — and the guard is what blocks open-coding
/inlining of the accessor.

**Mechanism (extend the Phase G lattice):**

1. Recognize the IV shape in `N_FOR`: `int i = lo; cond i < n; step i++`
   (also `i <= n-1`, `i += k`). Record fact `i ∈ [lo, n)` *inside the body*.
2. Treat `xs.length` / `xs.Count()` as a symbolic bound when the receiver is a
   dense List/Set and **no mutating call** (`Add/Set/Insert/RemoveAt/Clear/
   Sort`) targets that receiver inside the loop (mutation kills the fact).
3. Where `i < n` and `n <= xs.length` (same-symbol comparison or `n` literally
   `xs.Count()`), stamp `elide_oob_p` + `DEREF_GUARD_SAFE` on `xs[i]`,
   `xs.Get(i)`, `xs.GetMut(i)` — and emit the open-coded dense load path that
   gen already has.
4. Also adopt ConstraintElimination's dominated-condition trick at AST level:
   after `if (i < n) …`, uses of `i` in the then-arm carry `i ≤ n-1`
   (midopt already does this for nullness via `midopt_refine_cond`; extend to
   relational facts).

**Locus:** `src/midopt.c` (extend `struct midopt_fact` with symbolic
`lt_bound`/`ge_bound` fields + IV recognition in `midopt_safety_stmt` `N_FOR`);
gen consumes existing stamps (no gen changes needed).

**Impact:** spaceway3k draw/tick/HUD loops (thousands of guarded `Get`/`GetMut`
per frame), db-engine `_LowerBound`/`_UpperBound` binary search and
intersection scans, every user `for (i…Count())` loop. Unlocks R2 and R4.

**Risk:** medium — length-invalidation on mutation is the one real hazard;
kill-facts on any method call with the receiver as argument (the existing
"calls kill facts" rule) plus an explicit mutator-name list.

### R2 — Borrow-don't-copy by-value class elements on read paths

**STATUS: landed 2026-07-18 (Phase J).**  for-in by-ref 2.9× on 64B elements,
list.h HOFs 1.5× CountWhere, capturing-lambda GetMut-deref (0 copies).  Needs
the new `is_move_only<T>()` intrinsic (also landed).  See GEN-OPT-FINDINGS.md
§16.  Deferred: Sort comparator deref, borrow-params (R2.3), Map for-in by-ref.

**LLVM/GCC analog:** C++ copy elision / NRVO and `const&` passing in Clang;
LLVM `SROA` + `ArgumentPromotion`; GCC `-fipa-sra`. The principle: never
materialize a copy whose fields could have been read in place.

**Problem today (measured in examples):** class elements are copied by value
on every read path — `List<Ship>::Get` block-copies ~32 B (spaceway3k `Ship`
≈120 B), then by-value params copy again:

- spaceway3k: draw loop `Ship s = fleet.Get(i)` + `DrawShip(vg, s)` ≈ 2 copies
  ×1000 ships/frame; **8× `CountWhere` full scans** of 1000 ships/frame each
  doing a `Get` copy + by-value lambda param copy ≈ 16 000 struct copies/frame;
  `Where`+Shell-`Sort` of ~1000 Ships with a by-value-T comparator (~240 B per
  compare) to read the top 6.
- aurora-ops/neon-grid: same shape at N=8 — the *idiom* every user copies.

**Mechanism (three escalating cuts):**

1. **for-in loop var by-ref.** Dense for-in already computes `data+i`
   (GEN-OPT-FINDINGS §5.1). When the loop var is (a) never assigned, (b) never
   address-taken except into `borrows` calls, (c) the collection sees no
   mutating call in the body — bind the var as an **alias**: rewrite field
   reads `s.f` → `(*(data+i)).f` and method calls `s.Foo()` on a read-only
   var → receiver `data+i`. Midopt proves, gen emits.
2. **Open-coded HOF lambda params.** Capturing `Where/Find/Sort/Any` are
   already open-coded at the call site; the remaining copy is the lambda's
   by-value param slot initialized from `Get(i)`. Same read-only proof →
   substitute param field reads with `data[i].field`. For `Sort`, the
   comparator becomes `(data[i].field < data[j].field)` with no element copies;
   the Shell-shift moves stay (they are real writes).
3. **By-value class function params** (`DrawShip(vg, s)`): pass pointer when
   the callee provably doesn't mutate/escape it. Needs IPA borrow info —
   ownership.c **already infers** `PA_BORROWS` for pointer params; extend to
   by-value class params. ABI-visible; gate behind a flag at first.

**Locus:** `src/midopt.c` (proof) + `src/classyc.c` gen `N_FORIN` dense branch
and HOF open-code emission (rewrite); headers untouched for (1)(2).

**Impact:** the largest single hot-loop win available (kills ~10⁴ copies/frame
in spaceway3k); also shrinks stack traffic in every LINQ-style pipeline.

**Risk:** copy semantics are *snapshot* semantics — the rewrite is only valid
under the read-only + no-mutation proof. Conservative defaults; `-v` report of
by-ref bindings so users can audit. Sub-item 3 deferred/opt-in.

### R3 — Map/Set probe fusion and entry-handle APIs

**LLVM/GCC analog:** GVN/PRE over `readonly` calls (redundant call
elimination); in library design terms, `unordered_map::try_emplace` /
hashbrown's `Entry` API — one probe, then read or write through the handle.

**Problem today (from `include/map.h`):** every op re-hashes string content
(FNV-1a) and re-probes:

| Idiom | Probes today | Where |
|-------|---:|-------|
| `m[k] = m[k] + 1` | 2 (Get+Set) | aurora `board["SCOUT"]`, spaceway3k `presence[pid]` |
| `Contains(k)` then `Set(k,v)` (`TryAdd`) | 2 | map.h:440 |
| `GroupBy` per element | 3 (Contains+Set+GetMut) | map.h GroupBy |
| `GetOr(id,NULL)` then `docs[id] = …` | 2 | db-engine Insert/Delete |
| `Contains` + `presence[p.id]` per planet per pass | 2 | spaceway3k presence ranking |

**Mechanism (two layers):**

1. **Headers:** add single-probe primitives — `V* EnsureSlot(K)` (find or
   insert-default, one hash) and `bool FindSlot(K, V** out)`. Rewrite `TryAdd`,
   `m[k]=` (Set path), `GroupBy`, and `GetOr`-then-`Set` patterns in
   list/map/set headers on top of them.
2. **Midopt peephole:** in one basic block with no intervening mutator,
   `m.Contains(k)` followed by `m.Get(k)`/`m.Set(k,…)` with an *identical* key
   expression → stamp the second op `GEN_SAFE_SKIP_NULL`-style to reuse the
   first probe's slot. (Layer 1 alone gets most of the win; layer 2 is
   opportunistic.)

**Locus:** `include/map.h`, `include/set.h` (+ midopt optional).

**Impact:** every Map-heavy path: spaceway3k `RebuildPresence` (3→1 probes per
ship), db-engine Insert/Delete/Update, GroupBy everywhere. 2–3× fewer hashes
on write-heavy map code.

**Risk:** low (header-internal; dense-layout ABI unchanged if slot arrays keep
their shape). Rehash invalidation must be respected — handles die at growth,
exactly like today's `GetMut` doc.

### R4 — Size-budget auto-inline + purity marks (feed LICM/GVN)

**LLVM analog:** inliner cost model + `Attributor` (`readnone`/`readonly`/
`argmemonly` inference); **GCC:** `ipa-inline` growth budget + `ipa-pure-const`
/ `modref`.

**Problem today:** only `Count/IsEmpty/Capacity` get `inline_p`. `IsHot`,
`IsAlive`, `Radius`, `ValueCompare`, field getters stay real MIR calls, and
**MIR LICM cannot hoist any call** (calls clear mem-availability), so
`fleet.Count()` in a loop header costs a thunked call per iteration.

**Mechanism:**

1. Midopt estimates method body size (statement/expression count); methods
   under budget with no loops/throws/`try` get `decl_spec.inline_p = TRUE`.
   (Investigate the known `MIR_INLINE` miscompile on pointer-into-buffer
   returns first — GEN-OPT-FINDINGS §10 — or AST-inline 1–3-statement bodies
   instead, which avoids MIR's inline path entirely.)
2. **Purity stamps:** midopt marks methods `pure_p` (only reads fields, no
   writes, no calls to impure runtime) — `IsHot`, `Count`, `ValueTypeRank`,
   `Max<T>` specializations. Near-term: midopt itself hoists a *pure*
   `recv.Count()` out of a loop header into a temp (AST LICM-lite). Long-term:
   MIR grows a `readonly` call attribute so GVN/LICM works across calls
   (GEN-OPT Phase E item).
3. **Header fast paths:** `List.Add` — inline `if (len < capacity)` and only
   call `EnsureCapacity` on the growth edge (list.h:343 today calls it every
   Add). Same for Map `Set` growth check.

**Locus:** `src/midopt.c`, `include/list.h`/`map.h`, later `ext/mir/mir-gen.c`.

**Impact:** removes call-per-iteration overhead everywhere (`Count()` headers,
predicates in `CountWhere` scans); feeds R1's symbolic bounds (`Count()` seen
as a hoisted temp is an easier symbolic bound than a call).

**Risk:** low for (3); medium for MIR inline (known chaining miscompile —
fix or avoid via AST-inline).

### R5 — Cached string hashes for `Map<String, V>` (hashbrown-style)

**Analog:** Rust hashbrown (hash stored in control bytes), Java HashMap
(cached `hash` field). Not an LLVM/GCC pass — a data-structure upgrade the
compiler can exploit.

**Problem:** String keys are re-hashed by content on *every* op (map.h:107).
db-engine hashes doc ids/field names on every request; spaceway3k hashes
callsigns per presence update.

**Mechanism:** store the 64-bit hash in a parallel dense array in Map (fits
the existing `table[]` + dense keys/vals layout), filled on insert. Probe
compares hash before `strcmp`. Optionally: hash field in the `String` header
itself (bigger ABI decision).

**Locus:** `include/map.h`/`set.h`.

**Impact:** multiplies R3 (probes get cheaper *and* fewer); biggest on
long-string keys (db-engine field names).

**Risk:** low-medium (header ABI only; memory +8 B/slot).

### R6 — Header micro-fixes with outsized payoff (do first)

Cheap, local, immediately measurable:

1. **`Where` double-Get** (list.h:588): `if (pred(this->Get(i)))
   result.Add(this->Get(i))` → `T v = this->Get(i); if (pred(v))
   result.Add(move v);` — halves copies and bounds checks per match.
   (`CountWhere` etc. same pattern.)
2. **`Add` capacity fast path** (R4.3).
3. **Open-coded `Sort` with in-place comparator:** when Sort's lambda is
   capturing/open-coded, compare `data[i]`/`data[j]` fields directly instead
   of copying two T's into param slots (spaceway3k's per-frame top-traders
   sort: ~240 B per comparison today).
4. **`First`/`Last` on proven-non-empty** (after `Count()>0` check) →
   open-code with `SKIP_OOB` (R1 machinery).

**Impact:** immediate 1.3–2× on copy-heavy pipelines; zero new analysis risk.

### R7 — Escape-stamp persistence → dead shells & stack promotion

**Analog:** Go escape analysis; HotSpot scalar replacement; LLVM `SROA`
after `mem2reg`.

**Problem:** `src/ownership.c` computes `escapes_p` / `unsafe_escape_p` /
`returned_p` per candidate and **discards them** with the candidate VARR
(confirmed by audit: only `own_deref_class` and `auto_release_call` cross the
pass boundary).

**Mechanism:**

1. **Persist:** stamp `decl->proven_no_escape_p` (and maybe
   `decl->proven_return_only_p`) from the candidate flags — a few lines in
   `try_synthesize_auto_release`'s neighborhood, zero new analysis.
2. **Consume (midopt):** `new List<T>(…)` bound to a non-escaping local →
   lower to a stack shell (ctor on slot + RAII dtor — semantics ClassyC
   already has). Kills malloc/free churn for per-frame temps (spaceway3k's
   `living`/`ranked`/`live` lists, db-engine's per-query shells).
3. **Consume (DCE):** a shell whose elements are written but never read on
   any path → drop the fills (dtor-aware; keep element dtors if non-quiet).

**Locus:** `src/ownership.c` (stamps), `src/midopt.c` (rewrites).

**Impact:** allocation-traffic reduction in frame/query loops; pairs with R2
(smaller live set of shells). Also unlocks future SBO (small-buffer
optimization) without new analysis.

**Risk:** medium — ownership's escape carve-outs (non-retaining consumer
table) must be re-audited for *promotion* (stronger claim than leak-fixing);
start with stamping + `-v` reporting only, promote in a later phase.

### R8 — Identical-code folding for monomorph twins (ICF)

**LLVM:** `MergeFunctions`; **GCC:** `-fipa-icf`; linkers: `gold/lld --icf`.

**Problem:** monomorphization stamps `List<int>` ≡ `List<unsigned>`,
`Map<String,int>` ≡ `Map<String,long>` bodies that differ only in type
metadata. AOT binaries carry each copy; JIT pays icache/compile per copy.

**Mechanism (escalating):**

1. **AOT today:** b2obj per-function sections + `-Wl,--icf=all` at final link
   — zero compiler work; measure on hello+List and aurora-ops.
2. **Module-level:** hash MIR function bodies (opcode+operand shape); alias
   duplicates at `MIR_finish_module` / b2obj time. Careful with data refs
   (type-name strings in error paths) — merge only functions with identical
   relocations, linker-style.

**Locus:** `src/b2obj.c` + docs first; `ext/mir` later.

**Impact:** AOT size and JIT compile time; complements P0 DCE (which removes
*unused* methods; ICF merges *used-but-identical* ones).

**Risk:** low for (1); medium for (2) (debug info/addr-of-function identity).

### R9 — String concat fusion (N-ary `+`, f-strings, join/ToJson chains)

**LLVM analog:** `SimplifyLibCalls` (strcat/strcpy chains, strcmp-to-memcmp);
C++ expression templates / `absl::StrCat`.

**Problem:** `a + b + c` builds two heap Strings; f-strings/`ToJson` chains in
aurora `ToString`, db-engine `NewDocId`/`ToJsonArray`/`Freeze` allocate
intermediates per segment.

**Mechanism:** midopt flattens left-leaning `N_CONCAT` trees into one
variadic "concatN" gen node; gen computes total length (strlen per segment,
or tracked lengths) then one alloc + memcpy per segment. F-strings already
know their segment count statically.

**Locus:** `src/midopt.c` (rewrite), `src/classyc.c` gen (`N_CONCAT`).

**Impact:** string-heavy request paths (db-engine) and banner/ToString code;
fewer arena checkpoints per statement as a bonus.

**Risk:** low — arena semantics unchanged (one tracked String instead of N).

### R10 — MIR backend deepening (already GEN-OPT Phase E; keep queued)

Ordered by ROI for ClassyC-shaped IR:

1. **Restricted mem-GVN at -O2** for non-escaping frame slots (LLVM mem2reg
   analog; today gated behind `-O3`).
2. **`readonly` call attribute** in MIR (unlocks GVN/LICM across `Count()`,
   hash helpers, `c2m_str_length`) — prerequisite for backend LICM gains.
3. **Counted-loop unroll ×2/×4** when trip is const and body tiny.
4. **b2obj reachability DCE** + `--gc-sections` guidance.

Out of scope (right product bet, per GEN-OPT): auto-vectorization,
graph-coloring RA, whole-program LTO.

---

## 2. Cross-cutting insights

### 2.1 The symbolic-bound upgrade is the keystone

Midopt's Phase G lattice is constant-interval only. Nearly every guard we want
to erase needs a *relational* proof (`i < xs.length`, `mid <= hi`). LLVM's
`ConstraintElimination` shows the shape: facts as `a <= b + offset` over
SSA values, checked by decomposition. The ClassyC-scale version needs no
solver — only:

- IV facts from `N_FOR` headers (`i ∈ [lo, n)`, step sign),
- same-receiver length coupling (`xs.Get(i)` vs `xs.Count()`),
- dominated `if` refinement for relational facts (extend `midopt_refine_cond`),
- kill rules on mutation (already the lattice's model for calls).

That one upgrade powers R1, R4.2 (hoisted Count as bound), and R6.4.

### 2.2 Ownership.c is a fact factory we throw away

Audit findings (what ownership.c computes per function, then discards):

| Computed | Discarded? | Optimizer use if persisted |
|----------|-----------|-----------------------------|
| `escapes_p` / `unsafe_escape_p` per candidate | yes | R7 stack promotion, dead shells |
| per-BB `state_in/out` lattice | yes (CFG destroyed) | shared CFG for midopt (FINDINGS §4: ownership = 36% of self-compile — rebuild is the cost) |
| `func_summary_t` (`PA_BORROWS/PA_RELEASES`, `returns_owned_p`) | per-TU only | R2.3 borrow-params, purity inference input |
| CFG builder (`cfg_build_node`) | private to ownership.c | midopt could ask ownership for "does a mutator run in this loop" instead of rewalking |

Cheapest first step: **stamps, not sharing** — persist per-decl escape verdicts
(R7.1) and expose 2–3 query helpers (`ownership_binding_escapes_p(decl)`).
Sharing whole CFGs is a refactor to do once R1/R2 justify it.

### 2.3 The thunk tax makes AST-inline the ClassyC answer

MIR calls go through hot-swappable thunks; direct calls fight JIT design
(GEN-OPT §2.5). So where LLVM leans on its inliner + direct calls, ClassyC
wins by **not emitting the call at all**: open-code dense accessors (done),
AST-inline tiny methods (R4.1), open-coded HOFs (done), for-in by-ref (R2).
Midopt is the right home for all of these.

### 2.4 Where each example wins

| Program | Top items | Why |
|---------|-----------|-----|
| spaceway3k | **R2, R1, R6.3, R3, R4** | ~10⁴ struct copies/frame; 8 full `CountWhere` scans; guarded Get/GetMut per ship; Where+Sort per frame; presence 3-probe pattern |
| db-engine | **R3, R5, R1, R9, R2** | string-key hash/probe per request op; binary-search guards; concat/JSON churn; IndexEntry value copies |
| aurora-ops / neon-grid | **R2, R6, R3, R1** | the by-value LINQ idiom itself (N=8 here — they are the *shape* of user code) |
| hello+List AOT | **R8, R10.4, (P0 landed)** | binary size |

---

## 3. Phased plan

| Phase | Content | Effort | Gate |
|-------|---------|--------|------|
| **H** — quick harvest | R6 header fixes (Where double-Get, Add fast path, Sort in-place cmp); R3.1 entry-handle APIs in map.h/set.h; bench harness (`cy-bench/`: spaceway3k-style sim loop headless, db-engine request loop) | ~1 wk | cy-validate 53 green; bench numbers recorded in GEN-OPT-FINDINGS |
| **I** — symbolic guards | R1 lattice upgrade + R4.2 hoisted-Count bounds; R6.4 | ~2 wks | validate + bugs/001 at -O2/-O3; `sketch-midopt-safety.cy` extended |
| **J** — borrow reads | R2.1 for-in by-ref; R2.2 lambda param substitution | ~3 wks | validate; spaceway3k frame-time delta; `-v` by-ref audit report |
| **K** — inline & purity | R4.1 auto-inline (AST-inline first; investigate MIR_INLINE chaining bug); purity marks + AST LICM-lite | ~2 wks | mir-stats call-count drop on aurora-ops |
| **L** — memory shape | R7.1 escape stamps → reporting; R5 hash cache; R9 concat fusion; R8.1 ICF via linker | ~3 wks | bmir/ELF size table; db-engine throughput |
| **M** — backend | R10 (mem-GVN subset, MIR readonly calls, unroll, b2obj DCE); R7.2 promotion; R2.3 borrow params (flag-gated) | research | per-item |

Suggested first commit: **Phase H** (headers + harness) — no compiler
changes, pure win, and it gives every later phase a measurement floor.

---

## 4. Measurement plan (extend GEN-OPT §6)

| Bench | Metric | Covers |
|-------|--------|--------|
| `cy-bench/spaceway-sim.cy` (headless extraction: 1000 ships × 8 CountWhere + Where+Sort + presence rebuild × 100 frames) | wall/ticks | R1, R2, R3, R6 |
| `cy-bench/db-engine-requests.cy` (insert 10k docs, 10k mixed queries) | req/sec | R3, R5, R9, R1 |
| `sketch/run-midopt-bench.sh` + `-fdump-mir-stats` | funcs/insns/calls | all |
| `examples/classy-aurora-ops.cy` wall (GEN-OPT-FINDINGS §8 method) | wall | end-to-end |
| AOT: `classyc -c` + `b2obj` + `size`/`nm` on hello+List | bytes | R8, R10.4 |
| Correctness gate | `cy-validate/run-validate.sh` (53), `bugs/run-bugs.sh` | every phase |

Success targets (12-month, revising GEN-OPT §6.3 upward where noted):

- spaceway-style frame loop: **≥3×** fewer bytes copied, ≥2× wall (R1+R2+R6).
- db-engine mixed requests: **≥2×** throughput (R3+R5+R9).
- AOT hello+List `.o`: ≥5× smaller than pre-P0 baseline (P0 + R8 combined).
- cy-validate stays 53/0/0 at every phase boundary.

---

## 5. Risk register (additions to GEN-OPT §9)

| Risk | Mitigation |
|------|------------|
| By-ref loop var changes snapshot semantics (R2) | proof gate (no mutation of var or collection in body); `-v` audit list; `-fno-midopt-borrow` escape hatch |
| Length invalidation by mutation inside loop (R1) | mutator name list + receiver-escape kill; conservative `Sort`/`Add`/`Set` detection |
| Escape verdicts too weak for promotion (R7) | stamps + report first; promotion only for `!escapes_p && !returned_p` locals |
| Header entry-handle changes break layout assumptions (R3/R5) | dense-array ABI kept; val-043/044/049/050 cover GetMut/for-in paths |
| ICF merges functions whose address is observed (R8) | merge only non-address-taken, non-exported monomorphs |
| MIR_INLINE chaining miscompile resurfaces (R4) | AST-inline for ≤3-statement bodies; MIR inline only after root-causing GEN-OPT-FINDINGS §10 |

---

## 6. LLVM/GCC pass → ClassyC map (quick reference)

| LLVM / GCC pass | What it does there | ClassyC adaptation | Item |
|-----------------|--------------------|--------------------|------|
| `InductiveRangeCheckElimination` | split loops so checks provably pass | IV facts in midopt lattice → `elide_oob_p` | R1 |
| `ConstraintElimination` | symbolic `a<b+off` proofs kill redundant checks (powers `-fbounds-safety`) | relational facts + dominated-if refinement | R1 |
| `GuardWidening` / CVP | merge/hoist guards along paths | one entry assert per counted loop | R1 |
| SROA / NRVO / copy elision | never materialize unobserved copies | for-in by-ref; lambda param subst; RVO already landed (prvalue bind) | R2 |
| `ArgumentPromotion` / GCC `ipa-sra` | pass fields/pointer instead of aggregate | borrow-params for read-only by-value class params | R2.3 |
| GVN/PRE + `readonly` attrs | redundant-call elimination | map probe fusion; purity marks | R3/R4 |
| `Attributor` / `ipa-pure-const` / `modref` | infer function effects | midopt purity stamps; MIR `readonly` call attr | R4/R10.2 |
| inliner cost model / `ipa-inline` | size-budget inlining | auto-inline heuristic / AST-inline | R4.1 |
| `MergeFunctions` / `ipa-icf` / `--icf` | fold identical function bodies | monomorph twin folding | R8 |
| `SimplifyLibCalls` | strcat/strcmp chain folding | concat fusion; first-char dispatch note for db-engine `$op` chains (source-level) | R9 |
| Go EA / HotSpot scalar replacement | stack-allocate non-escaping objects | ownership escape stamps → stack shells | R7 |
| mem2reg | promote slots to SSA | restricted mem-GVN at MIR -O2 | R10.1 |
| `LoopIdiom` | recognize memset/memcpy loops | dense fill/copy already `memcpy`-based (list.h) — keep | — |

---

*Bottom line: ClassyC does not need LLVM's infrastructure to get LLVM-class
results on generics code. The four costs that dominate real ClassyC programs —
by-value element copies, unproven guards, repeated probes, and un-inlinable
calls — are all provable at the typed-AST layer with facts midopt and
ownership already (or almost) compute. Build the symbolic-bound lattice, the
borrow-read rewrite, and the probe-fusing headers, in that order.*
