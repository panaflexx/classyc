# MIDOPT-C — Ordinary C in the mid-level optimizer

Last updated: 2026-08-26 (C-A…C-D + `-On` inliner balance)

Scope: what `src/midopt.c` already does for **plain C**, what it still leaves
on the table, and a ranked plan to extend it without turning midopt into a
second SSA engine.

Related: [`GEN-OPT.md`](GEN-OPT.md) (pipeline and product stance),
[`GEN-OPT-FINDINGS.md`](GEN-OPT-FINDINGS.md) (landed phases A–I),
[`GEN-OPT-RESEARCH.md`](GEN-OPT-RESEARCH.md) (R1 / IRCE analog),
[`../memory/optimizer-landscape.md`](../memory/optimizer-landscape.md)
(constraints: scalar `reg_p`, the two collection whitelists, R-LICM),
[`../memory/gen-emits-inline-via-inline_p.md`](../memory/gen-emits-inline-via-inline_p.md)
(`MIR_INLINE` vs `MIR_CALL`).

This is the C-side companion to those docs. The bulk of midopt *source* is
still class / List / Map; the P1 lattice itself is already C-shaped.

---

## 0. Executive summary

Midopt sits between ownership and gen:

```
parse → check → ownership → midopt → gen → MIR (-O2 SSA)
```

It **does not rewrite the AST**. It stamps attrs that gen already consults.

The gap is not “C is ignored.” The lattice walks every live `N_FUNC_DEF`
(including `main` and `static` helpers). The gap is that **most of its
consumers are class/List stamps**, so ordinary C still emits `_safety_trap`
sites MIR then cannot see through.

| Workload | Who already wins | Midopt’s remaining C job |
|----------|------------------|--------------------------|
| Scalar C, `-fno-exceptions` | MIR `-O2` (~0.85–0.95× gcc `-O2`) | Small-func `inline_p`; little else |
| Default ClassyC C (exceptions **on**) | Partial: const-index OOB, `if (p)`, counted `a[i]` loops | Prove the rest of the traps dead **before** they split the CFG |
| List/Map pipelines | P0 DCE, open-code, R1/R2, R-LICM | Out of scope here |

**Product stance (same as GEN-OPT):** do not become LLVM. MIR already does
GVN/CCP/copy-prop/DSE/DCE/LICM. Midopt should **make C look like the C MIR
already optimizes** — typed facts MIR will never see (array lengths, `if (p)`
dominance, IV vs bound) stamped onto nodes gen already honors.

Measure C wins with **exceptions on**. `-fno-exceptions` already deletes the
traps; that profile is the wrong A/B.

**2026-08-26:** C1–C8, C13–C17 landed in `src/midopt.c` + gen stamp
consumption. C9/C10 still open. See §7. Sketch
`sketch/sketch-midopt-c.cy`, validate `cy-validate/val-063-midopt-c.cy`.

**Tune (oggenc, 2026-08-26):** midopt, gen, and MIR share an `-On` inliner
lever with a hard compile-time cap (eager JIT/AOT of oggenc well under
~3 minutes). The hang was **not** midopt (~0.1 s) and **not** SSA/phi
(`-O1` never builds SSA). Two MIR bugs plus unbounded auto-inline:

1. `process_inlines` auto-inlined **`MIR_CALL`** of callees up to 500 insns
   with 5× caller growth and no hard cap.
2. `simplify_op` chased cyclic `ref_def` (export/forward) with no hop
   limit — oggenc `main` never returned from `MIR_link`.
3. Header `static inline` as `MIR_INLINE` made `-O0` the same shape until
   gen stopped emitting it below `-O2`.
4. C16 static DCE on a **pure-C** TU dropped live K&R helpers
   (`parse_options`); JIT jumped to NULL. C16 is now skipped when the
   method keep-set is empty.

| Layer | Job |
|-------|-----|
| **midopt** | Cost analyzer: AST→insn estimate × call sites, stamp `inline_p` cheapest-first until an `-On` extra-insn budget (6k / 20k / 50k at `-O1/2/3`). Count/IsEmpty/Capacity always. `__` names never auto-marked. |
| **gen** | `-O0`/`-O1` = `MIR_CALL` even for `inline`. `-O2+` emit `MIR_INLINE` for `inline` / midopt stamps (`gen_mir_inline_ok_p`). |
| **MIR** | `MIR_set_inline_level` from classyc/b2obj `-On`. Caps: max callee (`MIR_INLINE` vs auto-`MIR_CALL`), growth %, hard max func insns. Skip converts leftover `MIR_INLINE` to `MIR_CALL`. Hop limit (64) on `ref_def` walks. |

`classyc -v` prints frontend stages plus MIR simplify / inlines / gen and
the slowest generated functions. `-dg0` dumps every func; `B2OBJ_DEBUG`
prints the same split for AOT. `classyc-aot.sh -On` forwards `-On` to
**both** classyc and `b2obj`/`b2objmac` (default b2obj `-O3` if unspecified).

Safety lattice is skipped under `-fno-exceptions`. When C16 runs, it is
**before** safety.

Measured on oggenc.c (`-w -fno-ownership -fno-exceptions`), 2026-08-26,
current `bin/classyc` (no `-v`; `-v` inflates check by ~0.6s of dumps):

| Flag | Front-end `-c` | MIR simplify | MIR inlines | MIR gen (`-eg`) | Wall `-eg` to `main` | `b2obj` AOT | gcc `-c` |
|------|----------------|--------------|-------------|-----------------|----------------------|-------------|----------|
| `-O0` | 1.33 s | 45 ms | 11 ms, 0 inlined | 267 ms | **1.28 s** | 0.57 s | 1.09 s |
| `-O1` | 1.46 s | 45 ms | 11 ms, 0 inlined | 420 ms | **1.50 s** | 0.72 s | 2.93 s |
| `-O2` | 1.45 s | 46 ms | 13 ms, 149 inlined | 716 ms | **1.83 s** | 1.04 s | 4.93 s |
| `-O3` | 1.41 s | 45 ms | 25 ms, 428 inlined | 892 ms | **2.02 s** | 1.19 s | 7.81 s |

Encode wall (same wav, 8m37s, quality 3; oggenc Rate/bitrate can lie —
use `time`):

| | Wall (compile + `-eb` + encode) |
|--|----------------------------------|
| gcc (`bin/oggenc.gcc`, already built) | 2.95 s |
| classyc `-O3 -eb` | **6.08 s** |
| `-O2 -eb` | 7.07 s |
| `-O1 -eb` | 9.88 s |
| `-O0 -eb` | 11.25 s |

**`-O0`:** skip midopt, no inlining, fast gen.

**`-O1`:** midopt light (trivial free getters; C16 only if methods exist);
gen emits `MIR_CALL`; MIR RA; auto-`CALL` inline off (`max_call=0`).

**`-O2`:** `MIR_INLINE` + SSA. MIR caps: INLINE callee 80 / auto-CALL 16 /
growth 180% / max 3000 insns. Midopt nst≤3, 1 nested call, 20k extra budget.

**`-O3`:** INLINE callee 200 / auto-CALL 64 / growth 300% / max 8000.
Midopt nst≤8, 2 nested calls, 50k extra budget. 428 MIR inlines on oggenc.

---

## 1. Pipeline, flags, stamps

`src/midopt.c` is `#include`d into `src/classyc.c` next to `ownership.c` (not
a CMake TU). Default **on**; `-fno-midopt` skips; `-fmidopt` forces on.
`-On` is shared with MIR (`-O2` if unspecified). `-v` prints keep/dead +
safety counts, inline cost-analyzer summary, and (on `-eg`/`-el`) MIR
simplify/inlines/gen timings. `-fsafety-errors` promotes definite-UB
diagnostics to errors. `-fdump-mir-stats` dumps funcs / forwards / insns /
calls / `MIR_INLINE` plus the largest functions after gen.

`midopt_run` order:

0. Collect free-function inline candidates (cost analyzer commits later)
1. Seed method reachability from free-function bodies
2. Method-level worklist
3. Collection-helper fill + drain (skipped safely when keep-set is empty)
4. Collect method inline candidates; **commit** cheapest-first until the
   `-On` extra-insn budget (Count/IsEmpty/Capacity always)
5. P1 safety lattice (`midopt_safety_module`)
6. Structural elide walk (`this` / `&local` / const index)
7. R2 for-in by-ref (no-op on C)
8. C16 prune unreferenced user-level `static` free functions

When the keep-set is empty (no live class methods), steps 3–4 method
enumeration is skipped; free-func candidates still commit. P1 / elide /
R2 still run. **C16 does not run** on that path (pure-C amalgamations
mis-pruned live K&R helpers). C16 still runs when methods were kept.

### 1.1 Stamps gen consumes

| Stamp | On | Effect in gen |
|-------|----|----------------|
| `midopt_dead_p` | class method **or** user-level `static` free func (C16) | Skip body + MIR forward |
| `decl_spec.inline_p` | method or free func | `MIR_INLINE` instead of `MIR_CALL` |
| `elide_oob_p` | `struct expr` (`N_IND` / some `N_CALL`) | Skip `_safety_trap` OOB |
| `elide_div0_p` | `N_DIV` / `N_MOD` / assign forms | Skip div0 trap (C2) |
| `elide_shift_p` | `N_LSH` / `N_RSH` / assign forms | Skip shift-range trap (C2) |
| `elide_div_ovf_p` | divide nodes | Skip signed MIN/−1 overflow trap (C2) |
| `own_deref_class = SAFE` | `struct expr` | Skip null trap; **must not** override ownership `CHECK` |
| `hoist_call_p` | `struct expr` of an `N_CALL` | Memoize pre-header value (`gen_hoist_lookup`) |
| `byref_p` | for-in element `decl_t` | Bind by reference, not copy |

C2 stamps live on the **operator**, not the operand. Do **not** set
`const_p` on `N_ID` (or on `+`/`-` inside a loop): the same node can be an
lvalue (`i++`) or a per-iteration rvalue; folding it once miscompiles
(`mov` dest, gcc `20010129-1`). Singleton locals reach gen via these elide
flags and via index ival.

### 1.2 What midopt is *not*

- Not a CFG. Per-function forward walk with structured if-join. `goto` into a
  const-false region is preserved via `c11_has_label_p` (gcc c-tests).
- Not SSA. Check already puts scalar locals in MIR regs (`reg_p`) except
  inside `try` (longjmp). A mem2reg pass is redundant
  (`memory/optimizer-landscape.md`).
- Not an AST rewriter. Every C opt below should prefer a new stamp (or
  setting `const_p` on an existing expr) over cloning trees.

---

## 2. What we have now — ClassyC-only (context)

Recorded so C work does not confuse these with the C lattice.

| Item | Notes |
|------|--------|
| P0 dead methods | Seed from free funcs; never prune **exported** free funcs. C16 may prune user-level `static` when methods were kept |
| Open-code skip-keep | Dense `Count`/`Get`/… calls do not keep a MIR body unless address-taken |
| R1 symbolic `i < recv.Count()` | `elide_oob_p` on `xs.Get(i)` / `xs[i]` |
| R-LICM | `hoist_call_p` on invariant `Count()` only |
| R2 for-in by-ref | Dense by-value elements |
| `uniq_ptr_p` | Only `new T`, for trusting pointer receivers |
| Two collection whitelists | `midopt_safe_methods` (growth OK for OOB) vs `midopt_pure_coll_methods` (read-only, for R-LICM / R2). Do not mix them. |

Measured (List, not C): list-sum BMIR ~5× smaller (Phase F); Get-loop **3.4×**
(Phase I, `cy-bench/bench-iv-access.cy`).

---

## 3. What we have now — already applies to ordinary C

P1 walks every live function. A file with no classes still gets inlining +
the safety lattice.

### 3.1 Nullness (`MN_TOP` / `MN_NULL` / `MN_NONNULL`)

Locals keyed by declaration node (`midopt_id_decl`: `def_node` or
`u.lvalue_node`).

- `if (p)` / `if (!p)` / `if (p == NULL)` / `if (p != NULL)`
- `&x` and string literals are non-null; `this` is non-null (structural)
- `if (!p) return; *p` — then-arm whose last stmt is `N_RETURN` keeps the
  else env (`p` non-null)
- `p = malloc(...)` is **not** assumed non-null (correct; ownership /
  object-guards). The `if (!p) return;` pattern still works after the join
- `sizeof` / `_Alignof` / `_Generic` controlling expr are unevaluated
  (`sizeof(*p)` must not warn)
- Never stamp `SAFE` over ownership `DEREF_GUARD_CHECK`

### 3.2 Integer intervals

Locals + check-time `const_p` on the expression node:

- `int a[10]; a[3]` and `for (i = 0; i < 10; i++) a[i]` → `elide_oob_p`
- Measured: C-array counted loop **2.3×** (`cy-bench/bench-iv-access.cy`,
  Phase I)
- Entire interval outside `[0, len)` → definite-OOB diagnostic
- Exact divisor `0` / shift count fully out of width → diagnostic only
  (gen still emits the trap unless the **node** is `const_p`)
- Relational **then-arm** at `-O2+`: `if (i >= K)` raises `lo`; `if (i < K)`
  tightens `hi`. **Else-arm is not refined**
- Flexible array members are conservative (`p->a[i]` does not use the
  declared `1`; value-object `s.a[i]` may)

### 3.3 Counted C `for` / `while` IVs

Same `midopt_safety_for` / `midopt_safety_while` as List, with a **constant**
(or interval-known) bound:

```c
for (int i = 0; i < 512; i++) s += a[i];   /* elides OOB */
while (i < n) { …; i++; }                  /* if n’s interval is known */
```

`do { } while` is explicitly out of scope (first iteration unguarded).
`switch` is walked then **all intervals are killed**.

R1’s `sym_recv` is class `Count()`. A C array’s declared length is used
only through the constant/interval path in `midopt_check_ind_oob`, not as a
symbolic bound tied to a local `n`.

### 3.4 C11 dead arms

Shared with gen: `c11_cond_known` / `c11_dead_skippable_p` in `src/gen.c`.

- Const `if` / `?:` / `&&` / `||` / `while (0)` / `for (;0;)`
- Does **not** skip a region that contains a `goto` / `case` / `default`
  target (gcc `20040704-1`, `pr17078-1`)

Gen already omits those dead arms at emit time. Midopt uses the same
predicate so method-keep and safety do not walk unevaluated calls.

### 3.5 Free-function inlining

Candidates are collected (`midopt_mark_inline_free_funcs` + kept methods),
then `midopt_commit_inlines` stamps cheapest-first until the `-On`
extra-insn budget. Shape filter `midopt_should_inline_p`:

- Arithmetic or void return; no loops / `switch` / `try` (`hard`)
- `-O1` free funcs: trivial scalar getter only (nst≤1, ncall≤0)
- `-O2`: nst≤3, 1 nested call; `-O3`: nst≤8, 2 nested calls
- Extra-insn budget 6k / 20k / 50k (`cost × max(sites,1)`)
- No pointer-to-struct/class return (`Get`/`GetMut` chaining)
- Same-func recursion is **rejected** (C17)
- Names starting `__` are never auto-marked (header/`__builtin_*`)
- Count/IsEmpty/Capacity always (cost 0)

Keyword `inline` already sets `inline_p` in check. That is **not** the
same as MIR expansion: gen emits `MIR_INLINE` only at `-O2+`, and MIR
`process_inlines` may still skip by callee/growth/cap.

oggenc `-O2`: midopt marked 92 candidates (~15k estimated extra); MIR
expanded 149 call sites and skipped 874 callees. `-O3`: 140 marked, 428
expanded. See §0 for wall times.

### 3.6 Already done *outside* midopt (do not reimplement)

| Mechanism | Where | C relevance |
|-----------|--------|-------------|
| Expression `const_p` fold | check (`3+4`, `1<<5`) | Gen `push_const_val`; div/shift trap skip on literals |
| Scalar `reg_p` | `process_func_decls_for_allocation` | mem2reg redundant; `try` functions memory-home all scalars |
| C-array OOB emit / `elide_oob_p` consume | `gen_c_array_oob` | One-past-end `&a[n]`; FAM sibling capacity |
| Div0 / shift / signed MIN/−1 traps | gen `N_DIV` / `N_LSH` | Skip only if operand `const_p` |
| FAM length from const `malloc` | ownership | Midopt does not reuse this for `p[i]` |
| `memcpy` / `memset` for aggregates | gen | Not a midopt pass |
| `restrict` | parsed, ignored for AA | gen/MIR if ever |

MIR `-O2`: GVN/CCP, copy-prop, DSE, SSA DCE, LICM, combine, linear-scan RA.
`-O3` adds load/store GVN. Calls clear mem-availability.

---

## 4. What C still pays for

Default ClassyC is a C compiler **with traps on**. For:

```c
int sum(int *p, int n) {
  int s = 0;
  if (!p) return 0;
  for (int i = 0; i < n; i++) s += p[i];
  return s;
}
```

today:

- `if (!p)` *does* make `p` NONNULL → null trap on `p[i]` can go
- `p[i]` has **no declared length** → **OOB trap every iteration** (correct;
  unprovable without more facts)
- `i < n` is **not** an interval on `i` unless `n` itself has a constant
  interval
- every call kills ival/nullness on pointer and int args, including
  `strlen` / `memcpy`

Traps split blocks and look like calls, so they also **block MIR LICM/GVN**.
Eliding them is the C analog of R1 on `List`.

Other current holes (lattice already close):

| Idiom | Today |
|-------|--------|
| `a[i+1]` with `i ∈ [0,8]`, `a[10]` | Index node is `N_ADD`, not an `N_ID` — no ival |
| `p && *p` / `p && p[i]` | `&&` RHS walked in the **unrefined** env |
| `if (n > 10) n = 10;` then loop `i < n` on `a[10]` | Else-arm of `n > 10` is not `n ≤ 10`; join → TOP |
| `int n = 5; x / n` | Interval known; gen still emits div0 (no `const_p` on the `N_ID`) |
| `i += 2` | Kills ival (`++` updates only when `lo == hi`) |
| `if (!p) abort(); *p` | `abort` is not `return`; join → TOP |
| `switch (x) { case 0: … }` | Intervals killed after the statement |
| `for (int *q = a; q < a + n; q++) *q` | Not an IV (`i` must be `N_ID` vs a bound) |
| `static` unused helpers | C16 prunes user-level `static` **when methods were kept**; skipped on pure-C TUs |

---

## 5. Possible C optimizations (ranked)

Prefer stamps gen already honors. New flags only when gen has no hook
(`const_p` on a use, or a dedicated elide bit).

### 5.1 Finish the lattice we already have — highest ROI

**C1. Singleton ival → `const_p` on uses.**
If a local’s interval is `[k,k]` at a use, set `const_p` / `c.i_val` on that
`N_ID` (or a thin wrapper gen already folds). Unlocks `push_const_val`,
`c11_cond_known`, and the existing div/shift `const_p` shortcuts. This is
the one “rewrite-like” stamp that multiplies everything else.

**C2. Div/shift elision stamps.**
Lattice already knows `n ∈ [1,10]` and `sh ∈ [0,31]`. Add `elide_div0_p` /
`elide_shift_p` (or fold into C1) and consult them next to the `const_p`
tests in gen `N_DIV` / `N_MOD` / `N_LSH` / `N_RSH` (and the `*_ASSIGN`
forms). Also skip signed MIN/−1 overflow when the interval cannot be both
`MIN` and `−1`.

**C3. Interval arithmetic on index expressions.**
`midopt_check_ind_oob` only reads ival of the **index node**. `a[i]` works;
`a[i+1]`, `a[i-1]`, `a[2*i]` do not. Fold `+`/`-`/`*` of intervals (overflow
→ TOP). Same helper feeds C2.

**C4. `&&` / `||` RHS refinement.**
`if (p)` is handled. `p && *p` is not. Apply `midopt_refine_cond` to the
RHS env (then restore). Classic C.

**C5. Else-arm relational VRP.**
Relations refine **then only**, `-O2+`. Else of `i < n` should be `i ≥ n`;
else of `n > 10` should be `n ≤ 10`. Unlocks clamp-then-loop. GCC Ranger /
LLVM ConstraintElimination analog at AST level (same family as R1).

**C6. Compound-assign / `++` on intervals.**
Widen: `i += k` shifts `[lo,hi]` when `k` is a non-negative (or known)
constant; `++` already handles singleton. Killing ival is the conservative
fallback.

**C7. `i < n` for C arrays when `n` is a length local.**
R1’s `sym_recv` is class `Count()`. For `int a[N]; for (i = 0; i < n; i++)`
with `n ≤ N` proved, treat `n` as the array’s length symbol (the type
already has the bound). Same `elide_oob_p` gen already honors on `N_IND`.

### 5.2 C idioms the walk does not see — medium ROI

**C8. Noreturn / abort / assert.**
Treat `abort` / `exit` / `__builtin_unreachable` (and maybe `assert` when
it expands to abort) like `return` for env kill, so `if (!p) abort(); *p`
keeps NONNULL.

**C9. `switch` VRP.**
Per-case ival on the discriminant (`case 0:` → `x ∈ [0,0]`) instead of
killing all intervals. Feeds C1–C3. Still no goto-CFG: fallthrough must
join cases.

**C10. Pointer-walk loops.**

```c
for (int *p = a; p < a + n; p++) s += *p;
```

Not an IV today. Large fraction of real C. Harder than index loops (alias,
one-past-end). Dedicated shape, not a lattice tweak. Do **after** C1–C7.

**C11. `do { } while` IVs.**
Explicitly out of scope (first iter unguarded). Leave it.

**C12. Goto.**
Keep `c11_has_label_p` conservatism. A real CFG is ownership’s to reuse
(`DOC/GEN-OPT.md`); do not build a third one in midopt.

### 5.3 Calls: stop treating every C call as a clobber — medium ROI

Every `N_CALL` sets arg nullness/ival to TOP. That is why `n = strlen(s);`
and `memcpy` poison later proofs.

**C13. Pure/readonly libc whitelist.**
`strlen`, `memcmp`, `abs`, `fabs`, `sqrt`; `memcpy`/`memmove` as not killing
the *source*. Keep intervals/nullness across the call.

**C14. Hoist loop-invariant pure calls.**
Gen already implements `hoist_call_p` for **any** stamped call; only
`Count()` sets it. `strlen(s)` with unmutated `s` is the C analog of
R-LICM. Do after C13, or call-kill still erases the fact.

**C15. Heap array bound from const malloc.**
Do **not** assume `malloc` non-null. Ownership already recovers FAM length
from a const byte count; midopt could reuse that for `p[i]` on a `malloc`’d
array of known `nelem` (heap analog of fixed `a[N]`).

`restrict` is parsed and ignored. Teaching MIR alias tags from `restrict`
is a gen/MIR change, not midopt.

### 5.4 Size / compile — cheap, independent

**C16. Internal-linkage DCE.** Landed. User-level `static` free funcs can
be `midopt_dead_p`. **Not** run when the method keep-set is empty (pure C
amalgamations). Roots: `main`, non-`static`, initializer refs, `used_p`.
Names starting `__` and exported C functions are never pruned.

### 5.5 Classic local opts (GEN-OPT Phase D — still open)

| Pass | In midopt? | Notes |
|------|------------|--------|
| Stmt-level const prop | **C1** | Singleton ival → `const_p` |
| Dead `if (0)` | Already in **gen** | Midopt only needs better `const_p` feeding it |
| Copy-prop / CSE / DSE | **Weak** | MIR `-O2` already does these on scalars. AST copies help only when they enable a **trap elision** before gen |
| Strength reduction / unroll / vec | **No** | MIR or never |
| SROA of small structs | gen, later | |
| Empty memcpy/memset loops | gen builtin expand | |

GEN-OPT Phase D (“block-local const prop + DCE + dead store”) is still
unchecked. For C, **C1 is the useful slice**; a general AST DSE pass is
mostly duplicate MIR.

### 5.6 Inlining C further — landed as a three-layer lever

**C17. Budget / recursion.** Same-func recursion is rejected. `-O3` uses
nst≤8 / 2 nested calls; `-O2` stays nst≤3 / 1. The extra-insn cost
analyzer (`midopt_commit_inlines`) plus MIR callee/growth/cap is what
stops oggenc from exploding. Pointer-to-struct returns stay out
(`GetMut().Boost`).

Keyword `inline` at `-O0`/`-O1` is still a `MIR_CALL` (gcc-like). Expansion
starts at `-O2`. `classyc-aot.sh -On` must match b2obj’s `-On` (see §0).

---

## 6. What not to put in midopt

From `memory/optimizer-landscape.md` and `DOC/GEN-OPT.md`:

- **mem2reg** for scalars — already `reg_p`
- **full SSA / GVN** — MIR `-O2`
- **mem-GVN** — MIR `-O3` (ClassyC hot paths are already open-coded)
- **vectorize / unroll** — wrong product bet
- **assume `new` / `malloc` non-null** — UAF / object-guards
- **third CFG** — reuse ownership if switch/goto ever need it
- **whole-class expand** — already replaced by method-level keep (Phase F)
- **`MIR_INLINE` of Get/GetMut** — miscompiles pointer-into-buffer chaining

MIR still wins on pure arithmetic once traps are gone.

---

## 7. Implementation plan

Ordered by ROI / risk. Each item is a stamp-or-flag change plus tests.
Do not skip the MIR c-test gate: C opts that fire on gcc’s suite will
show up there first.

### Phase C-A — facts gen already almost consumes

- [x] **C1** Singleton ival on locals feeds C2/C3 (did **not** stamp `const_p`
      on `N_ID` — see §1.1)
- [x] **C2** Div/shift (and signed MIN/−1) elide when interval proves it
- [x] **C3** Interval math on `+`/`-`/`*` (and unary `-`, casts, `?:`)
- [x] **C4** `&&` / `||` RHS refine + join with pre-RHS env

### Phase C-B — VRP completeness

- [x] **C5** Else-arm relational refinement (comparison flip)
- [x] **C6** Compound-assign / `++` interval update
- [x] **C7** `i < n` on a fixed C array after clamp / known `n.hi` (existing
      IV ival + C5; no new Count()-style symbol)

### Phase C-C — calls and size

- [x] **C13** Libc purity whitelist; memcpy-family kills dest only;
      by-value ints never killed
- [x] **C14** `hoist_call_p` on loop-invariant pure libc bounds (`strlen`)
- [x] **C15** `heap_len` from const `malloc`/`calloc`/`realloc` → `elide_oob_p`
      on `p[i]`. **Partial:** gen does not emit OOB on raw `T*` (only decayed
      arrays), so the stamp is currently a no-op on malloc pointers.
- [x] **C16** User-level `static` DCE. Names starting with `__` are **never**
      pruned (`__thunk_dtor_*` is a gen-time ref; dropping it infinite-looped
      val-009). `main` is never pruned. **Skipped when the method keep-set is
      empty** (pure-C amalgamations: K&R `parse_options` was dropped). When it
      runs, `used_p` is a second keep.

### Phase C-D — new shapes (last)

- [x] **C8** Noreturn (`abort` / `exit` / `_Exit` / `quick_exit` /
      `__builtin_unreachable` / `__builtin_trap`) treated like `return`
- [ ] **C9** `switch` case VRP
- [ ] **C10** Pointer-walk loops (`p = a; p < a+n; p++`)
- [x] **C17** Recursion guard in `midopt_should_inline_p` (same-func call).
      `-O3` nst≤8 / 2 calls plus the extra-insn cost analyzer is the budget
      raise; MIR `process_inlines` is the hard cap.

Leave **C11** (`do`-while IV) and **C12** (goto CFG) unplanned.

### Tests

- Sketch: `sketch/sketch-midopt-c.cy` (`-v` prints elision count + dead static)
- Validate: `cy-validate/val-063-midopt-c.cy`
- Gates run this pass: val-009/010/048/057/062/063, full `val-*.cy` with
  per-file timeout (REQUIRES flags honored for 026/053), gcc `20000113-1`
  and `20010129-1` (hoist infinite-loop regression).

---

## 8. Correctness and measurement

### 8.1 Gates (every PR)

- `sh ext/mir/c-tests/runtests.sh ext/mir/c-tests/use-c2m-gen-O3 bin/classyc`
  — this is the C contract
- `sh cy-validate/run-validate.sh`
- `sh bugs/run-bugs.sh` — especially **001** at `-O2`/`-O3` (phi/addr) and
  **009** (shift range)
- `examples/run-examples.sh` if the stamp is visible to ClassyC code
- Safety elision **proof-only**; `-fno-exceptions` is not the A/B

### 8.2 What to measure

| Suite | What it tells you |
|-------|-------------------|
| `cy-bench/bench-iv-access.cy` `a[i]` loop | C-array OOB elision (baseline 2.3× already landed) |
| New sketches: `a[i+1]`, `p && *p`, clamp-then-loop, `x / n` | C-A / C-B |
| MIR `c-benchmarks` (sieve / nbody class) | Must not regress `-fno-exceptions` scalar C |
| `-v` midopt elision counts + `-fdump-mir-stats` | Trap sites gone, insn/call drop; MIR simplify/inlines/gen split |
| oggenc.c `-On -eg` / `-eb` wall | Inliner cap + encode (ignore oggenc Rate) |
| AOT `.bmir` / `.o` via `classyc-aot.sh -On` | C16 (ClassyC TUs only); b2obj `-On` must match |

Exceptions **on** is the ClassyC C dialect. Quote that in any bench write-up.

### 8.3 Risk notes

| Risk | Mitigation |
|------|------------|
| Setting `const_p` on an `N_ID` that is later assigned | Stamp the **use node**, not the decl; ival already killed on assign |
| Else-arm VRP wrong on `goto` | Same `c11_has_label_p` skip as dead-arm elim |
| Libc “pure” that writes through `restrict` dst | Whitelist is readonly / source-preserving only; `memcpy` dst still kills |
| `static` DCE drops a function used only from asm / leftover AST | Skip C16 on empty keep-set; `__` exemption; `used_p` backstop |
| Inline of recursive C | Same-func check in `midopt_should_inline_p` |
| `process_inlines` never returns / JIT NULL | Hop-limit `ref_def`; no `MIR_INLINE` below `-O2`; MIR callee/growth/cap |
| Overflow in interval `+`/`*` | TOP on any overflow; signed vs unsigned matching the type |

---

## 9. Code touch map

| Piece | Location |
|-------|----------|
| Lattice / IV / refine | `src/midopt.c` (`midopt_fact`, `midopt_refine_cond`, `midopt_check_ind_oob`, `midopt_safety_expr`, `midopt_safety_for`) |
| Free-func inline | `midopt_mark_inline_free_funcs`, `midopt_should_inline_p`, `midopt_commit_inlines` |
| MIR inline caps / stats | `ext/mir/mir.c` `MIR_set_inline_level`, `process_inlines`, `simplify_op` hop limit |
| `MIR_INLINE` vs `MIR_CALL` | `src/gen.c` `gen_mir_inline_ok_p` (`-O2+`) |
| AOT `-On` | `classyc-aot.sh` → classyc **and** `b2obj` / `b2objmac` |
| Dead-arm predicates | `src/gen.c` `c11_cond_known`, `c11_dead_skippable_p` |
| C-array OOB emit | `src/gen.c` `gen_c_array_oob` |
| Div/shift traps | `src/gen.c` `N_DIV` / `N_LSH` cases (~6482+) |
| Hoist consume | `src/gen.c` `gen_hoist_lookup` / `gen_hoist_store` |
| `reg_p` | `src/check.c` `process_func_decls_for_allocation` |
| FAM malloc size | `src/ownership.c` `ow_fam_*` / `ow_call_const_alloc_size` |
| Driver flags | `src/classyc-driver.c` `-fno-midopt`, `-On`, `-v` MIR stats |
| C-array bench | `cy-bench/bench-iv-access.cy` |
| MIR C tests | `ext/mir/c-tests/` |

---

## 10. Open questions

1. **C1 vs C2.** If singleton ival always becomes `const_p`, do we still
   need dedicated div/shift elide bits for *ranges* (`n ∈ [1,10]`, not
   `{5}`)? Yes — ranges are the common loop case. C1 is literals;
   C2 is intervals.
2. **Stamp on the use vs the decl.** Use-node only. A decl-wide `const_p`
   is wrong the moment the local is reassigned.
3. **Should `-O0` skip C-A/C-B?** Yes: midopt is skipped entirely at
   `-O0`. `-O1+` keeps the hardness split (relational refine `< 2`,
   `while` IV `< 1`). Do not invent a second `-fmidopt-c`.
4. **`assert` as noreturn.** Depends on whether `<assert.h>` in this tree
   expands to `abort` or `((void)0)` under `NDEBUG`. Gate C8 on the
   actual macro, or only match `abort`/`exit` by name.
)
