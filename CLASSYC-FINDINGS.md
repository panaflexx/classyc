# ClassyC Compiler Audit — Findings & Improvement Opportunities

Date: 2026-07-11.
Scope: parse/gen correctness, compilation speed, JIT/AOT linking, refactoring,
missing functionality. Everything marked **[measured]** or **[validated]** was
reproduced/verified on this machine; items marked **[inspection]** come from
code reading and need confirmation before acting.

Baseline used throughout: `./bin/classyc` (rebuilt during this audit),
`cy-validate` suite green (36/36 files) before and after the fix below.

---

## 1. FIXED during this audit: bug 001 — `-O2` loop miscompilation

**Symptom.** `while (j + 1 < n && !(s[j] == '\r' && s[j+1] == '\n')) j++;`
never stops at the CRLF (`got=5`, expected `2`). Correct at `-ei`, `-O0`,
`-O1`; wrong at `-O2`/`-O3`. Reproduced with stock `bin/c2m` too, i.e. a MIR
backend bug, **also present in upstream vnmakarov/mir** (code is identical) —
worth reporting upstream.

**Root cause** (full write-up: `bugs/001-NOTES.md`). In
`ext/mir/mir-gen.c`, the SSA-combine pass (address folding, `-O2+`) guards
against folding an address chain through a loop phi with `cycle_phi_p()`.
After `make_conventional_ssa`, a phi's operands are defined by copies at the
tails of the *predecessor* blocks, so the old check
(`operand defined in the phi's own block`) only detected **self-loop** phis.
For any loop body spanning ≥2 blocks (exactly what a two-load short-circuit
condition produces), the combiner folded `base + (phi + const)` into a mem
operand in a *later* loop block — after the back-edge copy had already
advanced the phi register. The load read `s[j+3]` instead of `s[j+2]`.
GVN (`-O2+`) is a prerequisite: it re-associates the second load's address to
`phi + const`, which is why `-O1` is unaffected.

**Fix applied** (`ext/mir/mir-gen.c`, `cycle_phi_p`): treat **every** phi as a
barrier for cross-BB address folding — in conventional SSA every phi
destination register is multiply-defined, so only same-block uses are provably
safe. Equivalent to guarding all 4 call sites (`var_plus_const`,
`var_mult_const`, `var_plus_var` ×2) with `def is MIR_PHI`. Cost: at most one
un-folded add per address when the chain crosses a phi (the combiner still
forms `mem[base,index]`).

**Validation [validated]:**
- `bugs/001` reproducer: FAIL → PASS at `-O2` and `-O3`.
- Trigger variants (`A && (B || C)` shape, `int` array instead of `char`,
  single-load control case): all PASS; gcc agrees.
- Full `cy-validate` suite after rebuild: **36/36 pass, 0 crash**.
- During root-cause work, the equivalent 4-site patch was validated against
  all 215 `ext/mir/c-tests/lacc` tests and the `c-benchmarks` set (base vs
  patched outputs identical).

---

## 2. Bug tracker status (`bugs/` re-run) [measured]

| Bug | Status | Notes |
|-----|--------|-------|
| 001 short-circuit `-O2` | **FIXED** (this audit) | keep as regression test |
| 002 asit/As\<Drawable\> dmul crash | no longer crashes (exit 0, prints `d = (nil)`) | convert to PASS/FAIL self-check or retire |
| 003 String OOB subscript | **stale test**: now fails to *compile* (`incompatible argument type`, `unknown String method 'Length'`) | update test; also see cascading-diagnostics note §6.4 |
| 004 List OOB Get | throws `List.Get oob` but **uncaught exception → `abort()`/core dump (exit 134)** | decide: uncaught-exception should print + `exit(1)`, not SIGABRT core |
| 005 negative index | PASS (catchable) | regression test |
| 006 map missing key | PASS (KeyException) | regression test |
| 007 null-deref | traps at runtime (`fatal: null-pointer dereference`) but **aborts with core dump**; no static warning as the filename implies | same uncaught-trap UX issue as 004; static diagnostic still missing |
| 008 signed div overflow | PASS (catchable) | regression test |
| 009 shift out of range | **LIVE BUG**: `x << 40` on 32-bit silently truncates, `>> -3` succeeds; no trap/diagnostic | needs a `gen_shift_range_check` analogous to `gen_div_zero_check` (src/classyc.c ~L22746) |
| 010 uninit read | PASS | regression test |
| 011 VLA negative size | PASS (but reported as "division by zero" — misleading exception text from `gen_vla_size_check`, L22808) | cosmetic: use a dedicated message |

Recommendation: make `bugs/` runnable as a suite (like `run-validate.sh`) so
fixed bugs become permanent regression tests and stale ones surface.

---

## 3. Compilation speed [measured]

### 3.1 Where the time goes

Self-compile (`./bin/classyc -w -I ext/mir -I include -I src -c src/classyc.c`),
~6.8 s total:

| Stage | Time | Share |
|-------|------|-------|
| preprocess | 841 ms | 12% |
| parse | 351 ms | 5% |
| check | 309 ms | 5% |
| **ownership** | **2 462 ms** | **36%** |
| **generate (incl. bmir write)** | **2 861 ms** | **42%** |

Small file (`val-032`): 334 ms total — pre 89 / parse 26 / check 25 /
ownership 73 / gen 120. Even small JIT runs carry the full std-header cost.

gprof (self-compile, `-O2 -pg` build): `analyze` (ownership.c) 13.6% self,
**294 963 calls**; `_reduce_dict_find_longest` (bmir write compression in
ext/mir) 8.4%; `mir_mum`/hash 7.6%; `get_type_alias_name` 6.3%
(**20 068 calls**); `ownership_walk` 3 calls × 165 ms cumulative;
`collect_candidates` 4.2%; `cs_get` 4.0 M calls; `reg_malloc` 2.7 M calls;
`str_hash` 498 k; `find_def` 259 k; `subtree_allocates_string_p` 611 k.

### 3.2 Ranked opportunities

1. **Ownership pass re-walks the whole module ≥3×** (`ownership_run`,
   src/ownership.c L3408: 2+ silent interprocedural inference passes + 1
   diagnostic pass; each runs `analyze_function` → candidate collection, CFG
   build, dataflow **from scratch** per function per pass). Fixes, in order of
   payoff [inspection, high confidence]:
   - Cache per-function results across passes; re-analyze in later passes
     *only* functions whose callee summaries changed (a worklist keyed by the
     call graph instead of whole-module re-walks).
   - Persist candidates/CFG per `FUNC_DEF` across passes (only dataflow facts
     depend on summaries; the CFG and candidate set never change).
   - Memoize `subtree_allocates_string_p` / `subtree_allocates_object_p` /
     `is_non_retaining_consumer` by node uid (611 k+ redundant subtree scans).
   - Expected: ownership drops from ~36% to low single digits; ~2 s saved on
     self-compile.
2. **bmir write compression** — `_reduce_dict_find_longest` is 8.4% of the
   whole compile. `MIR_write_module` uses ext/mir's `mir-reduce` LZ-style
   compressor. Add a knob (`-fno-bmir-compress` or a fast level) for
   dev/JIT-adjacent workflows; the AOT pipeline immediately feeds the .bmir to
   b2obj, so compression buys nothing there. ~0.5 s on self-compile.
3. **`get_type_alias_name` memoization** (src/classyc.c L20881): recursively
   serializes struct layouts to a string on **every aliased memory access**
   generated (20 k calls in self-compile), then interns via `MIR_alias`. Cache
   `MIR_alias_t` keyed by `struct type *` (types are interned/stable after
   check); invalidate never within a TU. ~6% of compile.
4. **Preprocessor throughput**: `cs_get` is called per character through a
   function pointer chain (4 M calls); `push_str_char` does UTF-8 handling per
   char. Buffered line reads + `memchr` scanning would help, but the bigger
   architectural win is a **pre-lexed prelude cache**: every compile re-lexes
   `<environment>`, `<exception-prelude>`, `string_builtins.h`
   (`add_standard_includes`, L9624) plus all of `include/*.h` and libc
   headers. Caching the post-lex token array (or post-parse AST) for immutable
   headers keyed by (path, mtime) would cut the fixed ~90 ms floor per JIT run.
5. **`reg_malloc` is malloc-per-node + a tracking VARR push** (L426): 2.7 M
   allocations per self-compile, freed one-by-one in `reg_memory_pop`. A
   bump-pointer arena (allocate pages, free wholesale) removes both the malloc
   traffic and the 2.7 M-entry tracking array. Nodes/tokens/types are
   never individually freed today except via marks — the model already is an
   arena, just implemented as tracked mallocs.
6. **Parallelism**: `-p` parallelizes across *source files/modules* (driver
   `C2MIR_PARALLEL`), so single-file compiles gain nothing. Per-function
   parallel MIR-gen would be the long-term lever for `generate`.

---

## 4. AOT linking: dead-function elimination [measured]

### 4.1 The problem, quantified

Hello-world using only `List<int>` `Add`/`Get` (`-I include`, list.h only):

- **139 functions** in the emitted module — including a **complete
  `List<String>` instantiation** (~70 methods) pulled in by the
  prelude/`SelectString` compat path, plus the full `List<int>` method set.
  Only ~12 functions are actually referenced from `main`'s call chain
  → **~90% dead code**.
- b2obj object: 83 KB, `.text` = 49 KB in **one monolithic section** with
  almost all symbols local (`nm`: 1 global, 12 local text syms visible) —
  so `gcc -Wl,--gc-sections` **cannot** prune anything today.

### 4.2 Design options

- **Option A (recommended): reachability sweep over MIR items in b2obj**
  before emission (`create_object_file_from_module`, src/b2obj.c L531).
  Roots: `main`, exported items, anything referenced from data items
  (`MIR_ref_data`), and the `cyreg` registry sections (function-pointer
  registrations — see `cyreg_get_set` in src/classyc-driver.c and the cyreg
  section writer in b2obj.c). Edges: scan each func's insns for
  `MIR_OP_REF` operands to func/proto/import items. This is self-contained,
  language-agnostic, and benefits every .bmir regardless of front end.
  Risks to handle: address-taken functions reached only via data relocs
  (covered by data-item roots), `dlsym`-by-name at runtime (none in the AOT
  runtime today, but keep a `--keep-all` escape hatch), DWARF info referring
  to dropped funcs.
- **Option B: per-function ELF sections** (`.text.<fn>` + matching relocs) in
  b2obj plus `-ffunction-sections`-style linking in classyc-aot.sh
  (`-Wl,--gc-sections`). More invasive in b2obj (symbol/reloc bookkeeping per
  section, DWARF ranges), but composes with LTO-style tooling and keeps policy
  in the system linker.
- **Option C: front-end pruning via `decl->used_p`** (src/classyc.c,
  struct decl L10568). Cheapest to start — skip MIR-gen of unused
  file-local/static and unreferenced generic-instantiation methods — and it
  *also* speeds up compilation (§3) and JIT startup (§5), since the functions
  are never generated at all. Needs care: methods reachable via interfaces /
  `any` thunks / registries must be considered used.

Best combined plan: **C for generated generic methods** (biggest bang: the
header collections are where the bloat originates) + **A as a backstop** in
b2obj for whole-program AOT.

---

## 5. JIT startup / linking [measured]

`./bin/classyc -I include hello-list.cy`:

| Mode | Time |
|------|------|
| `-eg` (eager gen) | 0.30 s |
| `-el` (lazy gen) | 0.17 s |
| `-ei` (interp) | 0.19 s |

Eager `-eg` machine-codes all 139 functions although ~12 run. Opportunities:

1. Make **lazy gen the default for run mode** (or auto-select `-el` when not
   benchmarking): near-2× startup on collection-using programs, and it scales
   with the std library growing.
2. Front-end `used_p` pruning (§4 Option C) shrinks what even `-eg` generates.
3. The fixed ~150 ms front-end floor (prelude + headers re-lex/check per run)
   is the other half of JIT startup — the header/prelude cache from §3.2(4)
   applies directly.

---

## 6. Parse/gen audit & refactoring opportunities [inspection]

1. **`src/classyc.c` is a 31 k-line single TU** (with `ownership.c`
   `#include`d at L30615). The single-TU build is a deliberate choice, but the
   file now contains preprocessor, lexer, parser, checker, ownership, MIR gen,
   dbinfo, and an LSP query API. Splitting into `#include`-composed sections
   with clear interfaces (keeping the single-TU compile) would preserve the
   build model while making the sections separately reviewable.
2. **~600 lines of hand-rolled runtime-import boilerplate**: `struct gen_ctx`
   carries ~60 `*_proto`/`*_item` pairs plus one `#define` accessor each
   (L20409-20710), and `dict_ensure_imports` / `string_ensure_imports` /
   `exception_ensure_imports` / `safety_ensure_imports` repeat the same
   proto+import dance. A small table (`{name, ret, args...}`) with a generic
   `ensure_import(id)` would collapse this and make adding runtime helpers a
   one-liner (the `[[builtin_method]]` registry already proved this pattern
   for String methods).
3. **`check()` (L15410-20258) and `gen()` (L25592-29329) are ~5 k-line switch
   functions.** Extracting per-node-kind handlers would enable targeted tests
   and reduce merge risk; today a single misplaced `break` is invisible.
4. **Diagnostic cascades**: bug 003 shows one bad expression producing 3
   stacked errors (`incompatible argument`, `not a structure`, `unknown
   String method`). Error-poisoning (mark expr type as error, suppress
   follow-ons) would improve UX.
5. **`-v` output is unusably noisy**: `symbol_insert:` tracing plus a full
   symbol/tpname dump print on every verbose compile (`symbol_dump` is called
   unconditionally under `-v`, L30662). Gate behind `-d`/log levels so `-v`
   gives the useful stage timings only.
6. **Uncaught exceptions / safety traps call `abort()`** (core dump, exit
   134) — see §2 items 004/007. A runtime top-level handler that prints the
   exception + stack trace and exits with a conventional code would make test
   harnesses and users happier.
7. **Known ownership-analyzer false positive**: loop reassignment
   `for { h = new; …; delete h; }` flags UAF on the back-edge
   (CLASSYC-CLEANUP.md); the CFG back-edge merge widens OWNED→MAYBE without
   honoring the kill from `delete` + reassign ordering. Worth a dedicated
   look when doing the §3.2(1) rework.

---

## 7. Missing functionality (consolidated)

From CLASSYC-CLEANUP.md gaps plus this audit:

- **Language**: `?.` / `??`, slice sugar `list[1..3]`, `operator+`,
  properties, `static const` fields, extension methods, list literals without
  `new`, spread in dict.
- **Generics**: `if constexpr` / `is_int<T>` (blocks `List.Range`-style
  bodies), method-call type inference at some sites, deeper nested-generic
  stress coverage.
- **Class model**: method declaration order still matters within a class
  (forward-reference pre-scan exists for class *names* — L9639 — but not for
  sibling *methods*).
- **Toolchain**: dead-function elimination (§4), header/prelude cache (§3),
  bmir-compression knob (§3), `bugs/` regression runner (§2), shift-range
  safety check (§2, bug 009), uncaught-exception exit path (§6.6),
  upstream MIR bug report for the §1 fix.

---

## 8. Validation summary

- Fix in `ext/mir/mir-gen.c` (`cycle_phi_p`) rebuilt via `make classyc`.
- `bugs/001` + 3 trigger variants: PASS at `-O2`/`-O3` (gcc-verified expected
  values).
- `cy-validate`: 36/36 pass, 0 crashed, after the fix.
- Equivalent patch previously validated against 215 lacc c-tests +
  c-benchmarks in a scratch build (outputs identical to base).
- All measurements in §3-§5 taken on this machine with the commands quoted.
