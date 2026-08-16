# Open issues — fixability, priority, usefulness

Date: 2026-07-15  
Source: `CLASSYC-FINDINGS.md` / `CLASSYC-CLEANUP.md` remaining items, probed
against `bin/classyc`, `include/{list,map,set}.h`, `src/classyc.c`,
`cy-validate/`, `examples/`.

**Scale**

| Score | Meaning |
|------:|---------|
| Usefulness | How much everyday ClassyC code / house-style story improves |
| Fixability | How confident we can ship a correct fix soon |
| Effort | S ≈ ≤1 day, M ≈ few days, L ≈ week+, XL ≈ multi-week / research |

---

## Priority board (do first → later)

| # | Issue | Useful | Fixable | Effort | Verdict |
|---|--------|:------:|:-------:|:------:|---------|
| **P1** | **Select method-generic on stack/`List` value receivers** | ★★★★★ | ★★★★☆ | M | **Do next** — real bug; house style already assumes it |
| **P2** | **Uncaught exception → `exit(1)` not `abort()`** | ★★★★☆ | ★★★★★ | S | **Quick win** — UX + bugs/004; one-line runtime |
| **P3** | **`bugs/` runner** (like `cy-validate`) | ★★★☆☆ | ★★★★★ | S | **Quick win** — keeps regressions honest |
| **P4** | **Shift-range safety (bugs/009)** | ★★★☆☆ | ★★★★☆ | S–M | Extend `_safety_trap`; mirrors OOB/div0 |
| **P5** | **Capturing `Find` / `FindOr` (and maybe `Select` open-code)** | ★★★★☆ | ★★★☆☆ | M | HOF table already has `HOF_FIND`; stubs with error |
| **P6** | **GroupBy without `owned` (value Map shell)** | ★★★★☆ | ★★★☆☆ | M–L | Useful; true nested `List` values blocked — phased plan |
| **P7** | **Full-expression temp dtor for pure rvalue chains** | ★★☆☆☆ | ★★☆☆☆ | L | Chains of *collections* already work; class-element temps are the hard case |
| **P8** | **Language sugar** (`list[1..3]`, `operator+`) | ★★☆☆☆ | ★★★★☆ | S–M | Pure sugar; Slice/Plus exist |
| **P9** | **Capturing `Sort` / array `.filter`/`.map`** | ★★☆☆☆ | ★★☆☆☆ | M–L | Nice; less frequent than Where |
| **P10** | **Self-referential free generics + `Max<int>(…)`** | ★★★☆☆ | ★★☆☆☆ | L | Unlocks free algorithms; deep parser/checker |
| **P11** | **JSON binder Phase 3** (`Map*`, `List<User*>*`) | ★★★☆☆ | ★★★☆☆ | M–L | Product surface for web apps |
| **P12** | **AOT DCE / ownership-pass speed** | ★★☆☆☆ (ship) ★★★★ (dev loop) | ★★☆☆☆ | L–XL | Important long-term; not house-style blockers |
| **P13** | **Fat escaping closures** | ★★☆☆☆ | ★☆☆☆☆ | XL | Deferred; Strategy A covers pipelines |
| **P14** | **`Map<G, List<V>>` true nested value buckets** | ★★★☆☆ | ★☆☆☆☆ | XL | Move-only nested in Map dense buffer fails today |

---

## What we can fix (with confidence)

### P1 — Select on stack value receivers  ★★★★★ useful

**Symptom (reproduced):**

```c
auto xs = List<int>();
xs.Add(1);
auto d = xs.Select<int>(times2);   // ERROR: class has no member Select
```

**Works if `List<T>*` specialization is primed first:**

```c
List<int>* warm = new List<int>{1};
warm->Select<int>(times2);         // primes method-generic monomorph
auto xs = List<int>();
auto d = xs.Select<int>(times2);   // OK

// Or: any fn taking List<T>* (aurora SeedFleet, neon SeedGrid)
void Seed(List<Ship>* f);
Seed(&fleet);
fleet.Select(...);                 // OK
```

**Why aurora “works” and neon open-codes LapSample:**  
Aurora/neon pass `List<Ship>*` / `List<Pilot*>*` into helpers → specialization
sees pointer receivers → method generics (including `Select<U>`) attach.  
`List<LapSample>` is **only** used as a stack value → `Select` missing →
open-coded loop.

**Fix sketch:** `sketch/sketch-select-stack-method-generic.md`  
**Probes:** `probe-select-*.cy` in this directory.

---

### P2 — Uncaught exception exit path  ★★★★☆ useful / ★★★★★ fixable

Today (`include/cyexc.h`):

```c
fprintf(stderr, "uncaught exception %u: %s …\n", …);
abort();   // core dump; same for _safety_trap uncaught path
```

**Target:** print the same diagnostic, `exit(1)` (or `_Exit(1)`).  
Optional: `#ifdef` keep `abort` under `-g`/`CY_EXC_ABORT=1` for gdb.

**Fix sketch:** `sketch/sketch-uncaught-exit.md`  
Touches: `cy_exc_throw`, `_safety_trap` uncaught branch only.

---

### P3 — bugs/ runner  ★★★☆☆ useful / ★★★★★ fixable

11 files under `bugs/`. Mirror `cy-validate/run-validate.sh`.  
Some tests expect PASS-with-throw, some are stale (003). Runner can classify
expected outcomes later; v1 = compile+run + exit code.

**Fix sketch:** `sketch/run-bugs.sh` (drop-in).

---

### P4 — Shift-range safety (bugs/009)  ★★★☆☆ useful / ★★★★☆ fixable

Existing guards: null, div0, OOB index → `_safety_trap`.  
Add reason code for shift (e.g. 5) and emit before `<<`/`>>` when exceptions
on (default). Constant shifts can be compile-time errors/warnings.

**Fix sketch:** `sketch/sketch-shift-range-guard.md`

---

### P5 — Capturing Find  ★★★★☆ useful / ★★★☆☆ fixable

`get_hof_kind` already returns `HOF_FIND`, but open-code path errors:

```c
/* classyc.c ~14696 */
error(..., "capturing lambda in Find is not supported yet …");
```

Implement like `Where` but bind first match + break. `FindOr` needs a default
arg (slightly more work). Capturing `Select` can be open-coded similarly to
`Map` HOF (build result List, `Add(proj)`).

**Fix sketch:** `sketch/sketch-capturing-find.md`

---

### P6 — GroupBy without `owned`  ★★★★☆ useful / ★★★☆☆ fixable (phased)

**Today:**

```c
owned auto by = roster.GroupBy(keyFn);  // Map<G, List<V>*>*
// delete via owned / ownsValues on buckets
```

**Blocker for ideal `Map<G, List<V>>`:**  
`Map` values that are move-only collections do not monomorphize cleanly
(probe: `Map<int, List<int>>` → cascade of copy-init / type errors in map.h).

**Phase A (shipable):** return **value** `Map<G, List<V>*>` (by-value map shell,
still pointer buckets + `ownsValues`). Call site:

```c
auto by = roster.GroupBy(keyFn);   // no owned; ~Map deletes buckets
```

**Phase B (hard):** true `Map<G, List<V>>` once Map dense buffer supports
move-only / nested collection values.

**Fix sketch:** `sketch/sketch-groupby-value-shell.md` + `sketch-groupby-value-shell.cy`

---

### P7 — Full-expression temp dtor  ★★☆☆☆ useful *now*

```c
// Collection chains already work (probed):
int n = xs.Where((int x) => x >= 20).Take(2).Count();  // OK
```

Remaining pain is **class prvalues** with nontrivial `~T` into `Add`/calls
(see `BY-VALUE.md` P0). Lower priority after Select/GroupBy shell.

---

## What we should **not** chase next

| Item | Why wait |
|------|----------|
| Fat closures | Strategy A covers LINQ; UI-callback story is a different language surface |
| True `Map<G,List<V>>` | Depends on move-only map values; Phase A gets 80% of ergonomics |
| AOT DCE / ownership speed | Compiler infra; parallel track, not product house-style |
| `if constexpr` / self-ref free generics | Large front-end; free GroupBy already exists as special case |
| Slice/`+` sugar | Trivial when wanted; not blocking |

---

## Recommended sequence (2–3 weeks of focused work)

```
Week 1
  ├── P2 uncaught exit(1)          [half day]
  ├── P3 bugs runner               [half day]
  ├── P4 shift guard               [1 day]
  └── P1 Select stack monomorph    [2–3 days]  ← highest product value

Week 2
  ├── P5 capturing Find (+ FindOr) [1–2 days]
  ├── P6 GroupBy value Map shell   [2–3 days]
  └── val-046 / update neon-grid open-code Select → real Select
      update aurora/neon GroupBy to drop owned

Later
  ├── Select open-code for capturing projectors
  ├── full-expr class temp dtor
  └── AOT DCE / ownership speed
```

---

## Usefulness summary (product lens)

| If we only ship… | User-visible win |
|------------------|------------------|
| **P1 Select** | `List<LapSample>` / pure stack DTO pipelines match README; no “open-code Select” comments |
| **P2+P3+P4** | Safer, cleaner failure mode; regression harness for `bugs/` |
| **P5 Find capture** | `Find((T x) => x.id == want)` without globals |
| **P6 GroupBy shell** | Last major LINQ op that still forces `owned auto` in showcases |

**Single best next engineering target:** **P1 Select stack monomorph** — it is a
correctness/consistency bug relative to the documented house style, not sugar.

---

## Sketches in this directory

| File | Covers |
|------|--------|
| `OPEN-ISSUES-PRIORITY.md` | This board |
| `sketch-select-stack-method-generic.md` | P1 root cause + compiler fix plan |
| `probe-select-*.cy` | Minimal repros (pass/fail matrix) |
| `sketch-uncaught-exit.md` | P2 |
| `run-bugs.sh` | P3 |
| `sketch-shift-range-guard.md` | P4 |
| `sketch-capturing-find.md` | P5 |
| `sketch-groupby-value-shell.md` | P6 design |
| `sketch-groupby-value-shell.cy` | P6 target API (aspirational) |
