# ClassyC Compiler Audit — Findings & Improvement Opportunities

Last updated: 2026-07-16 (capturing Sort/Select, stmtexpr call-arg slots,
scalar `move` pass-through, quiet POD move monomorph, `List*[i]` Get/Set sugar,
GroupBy Phase B nested `Map<G, List<V>>` / `List<List<T>>`, **for-in FDA
`func_scope_num` layout**, aligned **`type_size`** aggregate copies).

Scope: parse/gen correctness, compilation speed, JIT/AOT linking, refactoring,
missing functionality, and the **first-class by-value collection idiom**.

Items marked **[measured]** / **[validated]** were reproduced on this tree;
**[inspection]** is code reading. Product roadmap details live in
[`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md) and [`BY-VALUE.md`](BY-VALUE.md).

**Validate baseline:** `cy-validate` **53** `val-*.cy` files — full suite green
(incl. **val-051** MIR/`_Atomic`/`stdatomic.h`).
Showcases: `examples/classy-neon-grid.cy`, `examples/classy-aurora-ops.cy`.
Bugs harness: `sh bugs/run-bugs.sh`. Pointer-arg smoke:
`examples/test-generic-ptr-args.cy` (`List*[i]` Get/Set).

---

## 0. Current product status (by-value idiom) — **landed**

**Goal (met):** Local LINQ pipelines without `owned auto` on every step.

```c
// House style today (validated: val-040, val-042, val-046, val-049, val-050, neon-grid, aurora-ops)
auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));
auto quick = samples.Where((LapSample s) => s.IsQuick());  // value List, RAII
auto top   = quick.Take(3);
auto ms    = samples.Select((LapSample s) => s.ms);        // stack Select works

auto by = roster.GroupBy(keyFn);   // Map<G, List<V>> — nested List shells
// no owned on GroupBy

int want = 3;
int hit = xs.Find((int x) => x == want);     // capturing Find
int flip = -1;
xs.Sort((int a, int b) => flip * (a - b));   // capturing Sort
int mul = 10;
auto sc = xs.Select<int>((int x) => x * mul); // capturing Select

List<int> make() {
    auto a = List<int>();
    a.Add(1); a.Add(2);
    return a;                 // implicit move of move-only collection local
}
auto xs = make();             // prvalue bind — no shallow copy
```

**What landed (compiler + headers):**

| Piece | Where |
|-------|--------|
| Stack `List`/`Map`/`Set` + RAII dtors | `create_decl` / gen; headers |
| Move-only bare assign banned; `move` transfer | `class_is_move_only_collection_p` |
| Implicit move on `return local;` for collections | `check` N_RETURN rewrite → `N_MOVE` |
| Storage return/assign → `.Copy()` for move-only | N_RETURN / N_ASSIGN (unlink before graft) |
| Prvalue init bind (`auto x = f()` / stmtexpr) | `create_decl` `prvalue_init_p`; stmtexpr class result in **call-arg area** |
| Scalar `move` is pass-through | gen N_MOVE — no I64-zero of int slots |
| Value-returning `Take`/`Skip`/`Where`/`Copy`/… | `include/list.h`, `map.h`, `set.h` |
| By-value elements + `__destroy` | List/Map element destroy |
| Stack Map `m["string"]` | N_IND: no TM_CLASS ↔ pointer key swap |
| Frame layout for class methods | no 8-byte FUNC_DEF offset clobber of BLK params |
| `GetMut` / `[]` lvalue for by-value class elements | List + Map; value **and** pointer receivers; val-043 / val-044 |
| Capturing lambdas as direct HOF args (Strategy A) | open-code `Where`/`Filter`/`Find`/`Sort`/`Select`/…; val-042 |
| `try` + adjusted array params (`char *argv[]`) | `force_val` keeps pointer load; val-045 |
| **Stack `Select<U>` (method generics)** | intern `orig_name` (no stack buffer); val-046 |
| **Uncaught exception / safety trap** | `exit(1)` not `abort` (`cyexc.h`); val-047 |
| **Shift-range safety** | `_safety_trap(5)` on `<<`/`>>`; val-048, bugs/009 |
| **GroupBy nested List (Phase B)** | `Map<G, List<V>>` by value; val-049 / val-050 |
| **Dense `*(ptr+i)` + memcpy growth** | Nested List/Map shells; SSA/MIR-friendly |
| **mir-gen addr-elim** | `collect_addr_uses` no longer asserts on non-move VAR_MEM uses |
| **bugs/ runner** | `sh bugs/run-bugs.sh` |
| **for-in FDA outer→inner** | `func_scope_num` sort; body locals after for-in vars (§2a) |
| **Aligned class copies** | Local init / block_move use `type_size` (Ship 32) |

**Nested collections (Phase B) — landed:** `List<List<T>>`, `Map<G, List<V>>`,
GroupBy returns nested List shells. Library dense slots use `*(data+i)` /
`memcpy` growth (SSA-friendly); `List*[i]` remains Get/Set sugar. val-050.

**Still open (not blockers for everyday pipelines):**

1. Full-expression temp dtor for pure rvalue chains without intermediate names.  
2. Array `.filter`/`.map`.  
3. Element `.owns()` stays separate — views must not steal owns (already enforced).

---

## 1. FIXED: bug 001 — `-O2` loop miscompilation

**Symptom.** `while (j + 1 < n && !(s[j] == '\r' && s[j+1] == '\n')) j++;`
wrong at `-O2`/`-O3`. Also in stock MIR / upstream.

**Root cause:** `ext/mir/mir-gen.c` `cycle_phi_p()` only blocked self-loop phis;
combiner folded addresses across multi-block loop phis after GVN.

**Fix:** treat every phi as a barrier for cross-BB address folding.

**Validation [validated]:** bugs/001, trigger variants, cy-validate green.

---

## 2. Other correctness fixes (this tree)

| Item | Status |
|------|--------|
| List/Map OOB | Throw `OutOfBoundsException` / `KeyException` (not silent no-ops) |
| List frame + by-value BLK | Class method FDA no longer reserves FUNC_DEF; N_CLASS not a stack frame |
| Stack Map String subscript | N_IND: do not swap `TM_CLASS` with pointer key |
| Enum bare name + `typedef enum E E` | No S_REGULAR insert of N_ENUM tag (def_symbol null attr crash); tpname + S_TAG |
| havoc12 (garbled C) | Exit 1, no SEGV in def_symbol |
| test-list-stdlib OOB | Expects throws, not no-ops |
| Move-return + prvalue bind | Implicit `return move a` for move-only collections; `auto x = f()` |
| Value LINQ shells | `Where`/`Take`/`Skip`/`Copy`/… return `List`/`Map`/`Set` by value |
| GetMut / `[]` mutate buffer | By-value class elements; not a Get copy |
| Capturing HOF lambdas | Open-code at call site; **Find / Sort / Select**; non-capturing stay thin fn ptrs |
| try + `char *argv[]` | Adjusted array params: force_val loads pointer, does not take `&argv` |
| Stack Select monomorph | `uniq_cstr` on expr-context generic name + `orig_name` (val-046) |
| Uncaught throw / safety trap | `exit(1)` + message; optional `CY_EXC_ABORT` (val-047) |
| Shift OOB | Runtime trap reason 5 (val-048, bugs/009 **FIXED**) |
| GroupBy Phase B | Value `Map<G, List<V>>` nested List shells; no `owned` (val-049) |
| Nested collections | `List<List<T>>` / dense `*(data+i)` + memcpy; mir-gen addr-elim (val-050) |
| Scalar `move` | Pass-through for non-class operands (no I64-zero of int slots) |
| Quiet POD `move` | Intentional no-ops in monomorph; no warning spam on `int`/POD shells |
| Capturing HOF stmtexpr | Class result slot in call-arg area (not local FP overlap) |
| `List*[i]` sugar | Get/Set protocol (not raw array-of-List); `test-generic-ptr-args.cy` |
| **for-in body vs loop vars (FDA)** | Sort by `func_scope_num` (outer→inner), not parse `node->uid` — see §2a |
| **Aggregate local copy size** | `type_size` (aligned sizeof), not `raw_type_size` (e.g. Ship 32 not 28) |

---

## 2a. FIXED: for-in frame layout + `Ship` memcpy SEGV (aurora-ops) [validated]

**Symptom.** `examples/classy-aurora-ops.cy` (and min repros with
`GroupBy` + `for (auto k, v in by) { Ship leader = v.First(); … v.ForEach(…); }`)
flaked ~30–50% with SIGSEGV at section 4. ClassyC stacktrace blamed `main` at
the for-in line; gdb showed:

```text
#0  __memcpy_avx_unaligned_erms
    rdi = stack, rsi = 0x1, rdx = 0x20   // memcpy from address 1, size 32
```

JIT-generated code mixed **0x20** and **0x1c** for the same `Ship` type.
Compile-only (`-c`) never failed — bad runtime frame / copy sizes, not a
missing monomorph.

**Root cause (two related bugs):**

1. **FDA sort used parse `node->uid`.** `N_FORIN` is built *after* `P(stmt)`
   parses the body, so the body block has a **lower** parse uid than the for-in
   node. Layout processed body locals first, then for-in vars at the **same**
   outer offset → `Ship leader` and live `List v` shared `fp+72`. Writing
   `leader = First()` overwrote the List shell: `List.data` became `Ship.id`
   (`1` on little-endian) → next Get/ForEach `memcpy` from `0x1`.

2. **Local aggregate init used `raw_type_size` (28 for Ship)** while Get/First
   returns and BLK params used aligned **`type_size` (32)**. Truncated copies
   and inconsistent strides in ForEach/Find/GroupBy paths.

**Fix (`src/classyc.c`):**

* `decl_cmp` / FDA: order scopes by **`func_scope_num`** (assigned outer→inner
  in `create_node_scope` during check), then by size.
* Local aggregate `gen_initializer` / init size: **`type_size`**, not
  `raw_type_size`.

**Validation [validated]:** leader+ForEach repro 30/30; aurora-min2 20/20;
`classy-aurora-ops.cy` 15+ runs under `-eg`/`-ei`/`-el`/`-eb` with `-g`;
cy-validate **52/0/0**. MIR: distinct slots (`v` @72, `leader` @96), Ship
memcpys all 32.

---

## 3. Bug tracker status (`bugs/`) [measured]

| Bug | Status | Notes |
|-----|--------|-------|
| 001 short-circuit `-O2` | **FIXED** | keep regression |
| 002 asit/As Drawable | no longer crashes | convert to self-check or retire |
| 003 String OOB | stale compile expectations | update test |
| 004 List OOB Get | throws; uncaught → **exit(1)** | print + exit (no core by default) |
| 005 negative index | PASS | |
| 006 map missing key | PASS | |
| 007 null-deref | trap + exit(1) uncaught | weak static warn |
| 008 signed div overflow | PASS | |
| 009 shift out of range | **FIXED** | `_safety_trap(5)` |
| 010 uninit read | PASS | |
| 011 VLA negative size | PASS | message still poor |

Runner: `sh bugs/run-bugs.sh` (mirrors `cy-validate`).

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
* Ownership loop false UAF on alloc/delete reassignment  
* Synthetic lambda bodies for open-code must be **N_BLOCK** (`{ return e; }`),
  never a bare expr — `N_FUNC_DEF` uses the body as the function scope  
* Stmtexpr class results: reserve in **call-arg area** (locals use FDA offsets;
  check-time bumps of `func_block_scope->size` are discarded at allocate)  
* FDA order must follow **check-time nesting** (`func_scope_num`), not parse
  uid — for-in (and any scope node allocated after its body) will otherwise
  collide body locals with loop vars (§2a)  
* Prefer **`type_size`** over **`raw_type_size`** for by-value class
  block_move / local init (trailing padding is part of the object for BLK ABI)  

---

## 7. Missing functionality (consolidated)

### A. By-value collections — remaining polish

* Full-expr temp dtor for pure rvalue chains  
* Dual `WherePtr` only if migration needs heap-pointer returns  

### B. Language / ergonomics

* Slice sugar `list[1..3]`, language `operator+`  
* Properties, extension methods, list literals without `new` where useful  
* Dict spread / array-literal assignment into `dict`  
* Method declaration order independence inside classes  
* Array `.filter`/`.map`  
* Escaping fat closures for stored UI callbacks  

### C. Generics

* `if constexpr` / `is_int<T>` for type-conditional bodies  
* Stronger call-site inference for some method generics  
* Self-referential free generic signatures (`List<T> Sort<T>(List<T> xs)`)  
* Explicit type args at call site (`Max<int>(3, 5)`)  

### D. Toolchain

* AOT DCE, header cache, ownership-pass speed  
* Retire/update stale `bugs/` (002, 003)  

---

## 8. Design note (for reviewers)

**Value shells + explicit element ownership is the right default.**  
The product stance (C++/`vector` RAII) is implemented for everyday
`Where`/`Take`/`Copy`/`GroupBy` pipelines. Developers should reach for stack
locals first.

`owned` remains correct for:

* heap escape across lifetimes  
* interfaces that intentionally return `List*` / `Map*` (e.g. `String.split`,
  JSON binder collection fields). GroupBy returns a **value** Map whose
  buckets are nested **value** `List<V>` shells (Phase B) — no `owned` at the
  call site.

**`[]` sugar:** developers expect `list[i]` and `list_ptr[i]` to mean Get/Set.
Keep that. Nested dense buffers are a library problem (`*(data+i)`), not a
reason to treat `List*` as a C array of Lists.

---

## 9. Validation summary

* mir-gen `cycle_phi_p` fix; rebuild `make classyc`  
* bugs/001 variants PASS at -O2/-O3; bugs/009 shift PASS  
* cy-validate: **53** `val-*.cy` files, full suite green (val-051 atomics)  
  * val-038…045 — by-value / GetMut / capture / try-argv  
  * val-042 — capturing Where/Filter/Map/ForEach/Any/All/**Find/Sort/Select**  
  * val-046 stack Select (no prior `List*`)  
  * val-047 uncaught → exit(1)  
  * val-048 shift-range guard  
  * val-049 / **val-050** — GroupBy + nested `List<List<T>>` / `Map<G,List<V>>`  
* neon-grid / aurora-ops: stack List/Map; Select on LapSample; GroupBy without `owned`  
* aurora-ops for-in + `Ship` body local: **stable** after FDA `func_scope_num` +
  `type_size` fix (§2a); modes `-ei`/`-eg`/`-el`/`-eb`  
* `examples/test-generic-ptr-args.cy`: `List<char*>*` bracket Get/Set  
* `sh bugs/run-bugs.sh` available  

## 10. Uninitialized `expr` bit-field → phantom LICM hang (2026-07-25) [validated]

**Bug:** `gcc/20010129-1.c` hung at -O1+ (infinite loop in `foo`'s while).

**Root cause:** `create_expr` (`classyc.c`) does not zero-fill (`reg_malloc`),
and `hoist_call_p` was added to `struct expr` (midopt R-LICM) without being
initialized there.  Garbage in that bit randomly marked ordinary calls
(`baz1`, even `abort`) as "loop-invariant pure": gen then memoized the
call's pre-header result (`gen_hoist_store`) and the while back-edge
condition reused the **stale** value instead of re-calling — the loop guard
never changed → infinite loop.  Flaky/body-dependent because it depended on
heap garbage (an unrelated inner `for` changed the allocation layout and
flipped the bit).

**Fix:** one line — `e->hoist_call_p = FALSE;` in `create_expr`.  Any new
`struct expr` field must be initialized there (the function already warns
about zero-fill for `def_node`).

**Verified:** 20010129-1.c exits 0 at -O1/-O2/-O3; full sweep of 655
gcc c-tests: 0 hangs / 0 crashes; cy-validate 58/58 green.

**Known separate issue (upstream, not classyc):** at `-O0`, any
address-of-local (`void *p = &n;`) trips
`mir-gen.c: transform_addr: Assertion 'loc > ST1_HARD_REG'`.  Reproduces
with upstream `c2m` too — pre-existing MIR backend bug, out of classyc scope.

---

Related: [`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md), [`BY-VALUE.md`](BY-VALUE.md),
[`GENERICSMEM.md`](GENERICSMEM.md), [`LAMBDA-CAPTURE.md`](LAMBDA-CAPTURE.md),
[`sketch/OPEN-ISSUES-PRIORITY.md`](sketch/OPEN-ISSUES-PRIORITY.md).
