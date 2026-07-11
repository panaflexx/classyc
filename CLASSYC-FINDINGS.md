# ClassyC Compiler Audit — Findings & Improvement Opportunities

Last updated: 2026-07-11 (by-value / stack collections / transform-return target).

Scope: parse/gen correctness, compilation speed, JIT/AOT linking, refactoring,
missing functionality, and the **first-class by-value collection idiom**.

Items marked **[measured]** / **[validated]** were reproduced on this tree;
**[inspection]** is code reading. Product roadmap details live in
[`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md) and [`BY-VALUE.md`](BY-VALUE.md).

**Validate baseline:** `cy-validate` **41 files** green after by-value/stack work;
`examples/classy-neon-grid.cy` uses stack Lists/Maps.

---

## 0. Current product target (by-value idiom)

**Goal:** Local LINQ pipelines without `owned auto` on every step.

```c
// Target (not fully implemented for transforms yet)
auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));
auto quick = samples.Where((LapSample s) => s.IsQuick());  // value List, RAII
auto top   = quick.Take(3);

// Today (honest)
owned auto quick = samples.Where(...);  // List* heap shell
```

**Blockers (see CLEANUP “First-class by-value idiom”):**

1. Move-return / bind of move-only `List`/`Map`/`Set` (`return move a` → `auto x = f()`).  
2. Headers still return `List<T>*` from `Take`/`Skip`/`Where`/`Copy`/…  
3. Element `.owns()` semantics stay separate — views must not steal owns.

**Already landed (supports the target):**

* Stack `List`/`Map`/`Set` + RAII dtors  
* Move-only bare assign banned  
* By-value elements + `__destroy`  
* Stack Map `m["string"]` (N_IND no longer swaps class with pointer key)  
* Frame layout for class methods (no 8-byte FUNC_DEF offset clobber of BLK params)

---

## 1. FIXED: bug 001 — `-O2` loop miscompilation

**Symptom.** `while (j + 1 < n && !(s[j] == '\r' && s[j+1] == '\n')) j++;`
wrong at `-O2`/`-O3`. Also in stock MIR / upstream.

**Root cause:** `ext/mir/mir-gen.c` `cycle_phi_p()` only blocked self-loop phis;
combiner folded addresses across multi-block loop phis after GVN.

**Fix:** treat every phi as a barrier for cross-BB address folding.

**Validation [validated]:** bugs/001, trigger variants, cy-validate green.

---

## 2. Other correctness fixes (recent, this tree)

| Item | Status |
|------|--------|
| List/Map OOB | Throw `OutOfBoundsException` / `KeyException` (not silent no-ops) |
| List frame + by-value BLK | Class method FDA no longer reserves FUNC_DEF; N_CLASS not a stack frame |
| Stack Map String subscript | N_IND: do not swap `TM_CLASS` with pointer key |
| Enum bare name + `typedef enum E E` | No S_REGULAR insert of N_ENUM tag (def_symbol null attr crash); tpname + S_TAG |
| havoc12 (garbled C) | Exit 1, no SEGV in def_symbol |
| test-list-stdlib OOB | Expects throws, not no-ops |

---

## 3. Bug tracker status (`bugs/`) [measured]

| Bug | Status | Notes |
|-----|--------|-------|
| 001 short-circuit `-O2` | **FIXED** | keep regression |
| 002 asit/As Drawable | no longer crashes | convert to self-check or retire |
| 003 String OOB | stale compile expectations | update test |
| 004 List OOB Get | throws; **uncaught → abort/core** | prefer print + exit(1) |
| 005 negative index | PASS | |
| 006 map missing key | PASS | |
| 007 null-deref | trap + abort/core; weak static warn | |
| 008 signed div overflow | PASS | |
| 009 shift out of range | **LIVE** | need shift-range check |
| 010 uninit read | PASS | |
| 011 VLA negative size | PASS | message still poor |

Make `bugs/` runnable like `cy-validate`.

---

## 4. Compilation speed [measured]

Self-compile of `classyc.c` ~6.8 s: ownership ~36%, generate ~42%.

Opportunities (unchanged ranking):

1. Ownership: worklist + cache CFG/candidates across passes  
2. BMIR compression knob for non-shipping builds  
3. Memoize `get_type_alias_name`  
4. Pre-lex/prelude cache  
5. Bump arena instead of tracked `reg_malloc`  
6. Parallel per-function gen for multi-file / large TUs  

---

## 5. AOT / JIT dead code [measured]

Hello-world + `List<int>` still pulls huge generic graphs (~90% unused methods).

* b2obj reachability DCE  
* Per-function sections + `--gc-sections`  
* Front-end prune via `used_p` on monomorphized methods  

Lazy JIT (`-el`) remains a strong default for run mode.

---

## 6. Parse/gen & structure [inspection]

* `classyc.c` monolithic — compose with includes, keep single TU if desired  
* Import boilerplate → table-driven ensure  
* Diagnostic poisoning for cascades  
* Uncaught exception / safety trap → clean exit path  
* Ownership loop false UAF on alloc/delete reassignment  

---

## 7. Missing functionality (consolidated)

### A. First-class by-value collections (priority)

See CLEANUP P0–P3:

* Move return of move-only List/Map/Set  
* Value-returning `Take`/`Skip`/`Where`/`Copy`/Map filters  
* Validate + neon-grid without `owned` on local pipelines  

### B. Language / ergonomics

* Slice sugar `list[1..3]`, language `operator+`  
* Properties, extension methods, list literals without `new` where useful  
* Dict spread  
* Method declaration order independence inside classes  

### C. Generics

* `if constexpr` / `is_int<T>` for type-conditional bodies  
* Stronger call-site inference for some method generics  
* `Select` edge cases on stack List + by-value T (workaround: open code)  

### D. Toolchain

* bugs/ runner, DCE, header cache, shift-range check, uncaught-exit UX  

---

## 8. Design note (for reviewers)

**Value shells + explicit element ownership is the right default.**  
Developers reach for simple RAII first; forcing `owned auto` on every
`Take`/`Where` re-introduces C-style ownership fatigue and fights the
C++/`vector` model already documented in BY-VALUE.md.

`owned` remains correct for:

* heap escape across lifetimes  
* interfaces that intentionally return `List*` / `Map*`  

---

## 9. Validation summary

* mir-gen `cycle_phi_p` fix; rebuild `make classyc`  
* bugs/001 variants PASS at -O2/-O3  
* cy-validate: **41 pass** after by-value/stack/Map subscript  
* neon-grid: stack List/Map + Pilot*.owns + LapSample by-value  
* val-038: stack List + Map string subscript  
* havoc1–13: exit 1 without crash  

Related: [`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md), [`BY-VALUE.md`](BY-VALUE.md),
[`GENERICSMEM.md`](GENERICSMEM.md).
