# Capturing lambdas — Strategy A (open-code / desugar)

**Status:** v1 landed (Strategy A open-code)  
**Choice:** **Option A** — call-site open coding; **no fat pointers**  
**Non-goal (v1):** escaping closures (`return [x]() => …`, stored `auto f = …`)  
**Related:** `examples/classy-lambda.cy`, `cy-validate/val-042-lambda-capture.cy`,  
`include/list.h` / `map.h` / `set.h`, `src/classyc.c`

---

## 0. Why this exists

ClassyC lambdas today are **hoisted static functions** that lower to a **thin
C function pointer**. That fits C/MIR and keeps `List.Where(int(*pred)(T))`
simple — but the body **cannot see enclosing locals**:

```c
int thr = 5;
// ERROR / wrong today if thr is used inside:
auto q = xs.Where((int x) => x > thr);   // thr not in lambda scope
```

Workarounds pollute real code (globals in `classy-docsearch.cy`, benches, etc.).

**Strategy A** fixes the high-value case — **literal lambdas as HOF arguments** —
by **not forming a function pointer at all**. Free variables stay normal names
in the parent frame. Call speed is optimal; ABI stays thin; blast radius is
confined to the check-phase desugar of known higher-order call sites.

Google-scale systems are still “list/map/filter then join then view” **on each
shard**; A is the right shape for that inner loop sugar. Fat closures remain a
later product for plugins / thread pools.

---

## 1. Current compiler reality (anchors in `classyc.c`)

| Piece | Location | Role today |
|-------|----------|------------|
| Token `=>` | `T_FAT_ARROW` ~L653 | Fat-arrow lexer |
| AST | `N_LAMBDA` ~L727 | Untyped / deferred lambda |
| Parse typed lambda | `lambda_expr` ~L6159–6212 | Builds static `N_FUNC_DEF`, returns `N_ID` |
| Parse body | `parse_lambda_body` ~L6128 | `{…}` or `return <expr>;` wrap |
| Build static fn | `build_lambda_func_def` ~L6108 | `static auto __lambda_N(…) {…}` |
| Pending inject | `pending_lambdas` in `parse_ctx` ~L4518, `#define` ~L4540 | Module list injection |
| UID | `lambda_uid` ~L4519 | `__lambda_%u` names |
| Instantiate untyped | `instantiate_lambda` ~L13698–13752 | Call-site monomorphization of params |
| Seq methods | `check_seq_method_call` ~L13466+, `SEQM_FILTER/MAP/REDUCE` ~L13486 | Array/slice `.filter/.map/.reduce` |
| Untyped outside HOF | `check` `case N_LAMBDA` ~L15769–15774 | Error unless filter/map/reduce arg |
| Gen | `gen` `case N_LAMBDA` ~L26748 | Evaluates to generated static func |
| Docs in tree | `examples/classy-lambda.cy` L9 | “No closures” |

**HOF signatures (stdlib — unchanged ABI for non-capturing):**

```c
// include/list.h
void    ForEach(void(*action)(T));
List<T> Filter(int(*pred)(T));
List<T> Where(int(*pred)(T));     // ~L442
List<T> Map(T(*fn)(T));
List<U> Select<U>(U(*fn)(T));
int     Any(int(*pred)(T));
int     All(int(*pred)(T));
T       Find(int(*pred)(T));
void    Sort(int(*cmp)(T, T));

// include/map.h
Map<K,V> Where(int(*pred)(K, V));
void     ForEach(void(*action)(K, V));

// include/set.h
Set<T> Filter(int(*pred)(T));
```

---

## 2. Product rules (v1)

### 2.1 Allowed (desugar)

A **capturing** lambda may appear **only as a direct argument** to a recognized
higher-order operator, where the entire call is still in the enclosing
function’s stack frame:

```c
int thr = 5;
auto xs = List<int>();
// … fill xs …
auto big = xs.Where((int x) => x > thr);          // OK — thr captured by open-code
xs.ForEach((int x) => { printf("%d\n", x + thr); }); // OK

String prefix = "err";
auto bad = logs.Where((String s) => s.starts_with(prefix)); // OK
```

Free variables resolve **lexically** in the parent function — same as writing
the loop by hand.

### 2.2 Forbidden (clear error)

```c
int thr = 5;

// Stored / named binding of a capturing lambda
auto pred = (int x) => x > thr;           // ERROR: capturing lambda must be HOF arg

// Escaping
auto make(int t) {
    return (int x) => x > t;              // ERROR: cannot return capturing lambda
}

// Indirect
int (*fp)(int) = (int x) => x > thr;      // ERROR
takes_pred(fp);

// Nested capture into a lambda that is itself stored — out of scope
```

Non-capturing lambdas keep working everywhere they do today (including
assignment to `int(*)(T)`).

### 2.3 Capture semantics (v1)

| Capture | Semantics |
|---------|-----------|
| **Scalars / pointers** | Read of enclosing automatic binding by **name** (open-code). Same as nested block. |
| **Mutations** | Allowed only while HOF runs in the same frame: `(int x) => { thr++; return x > thr; }` is OK for open-coded `ForEach`; not for returned closures. |
| **Globals / statics / functions** | No capture needed (already visible). |
| **`this` in methods** | Free use of `this`/members treated like enclosing “locals” of the method frame — must remain valid for the desugared loop (same as today for method-body code). |

No `[=]` / `[&]` syntax in v1. Optional later sugar:

```c
xs.Where([thr](int x) => x > thr);   // explicit; v1 can ignore brackets
```

---

## 3. Lowering model (Strategy A)

### 3.1 Source → open loop (Where / Filter)

**Before (desired surface):**

```c
int thr = 5;
auto q = nums.Where((int x) => x > thr);
```

**After desugar (conceptual C / AST equivalent):**

```c
int thr = 5;
auto q = List<int>();
/* receiver evaluated once */
auto __recv = /* nums, ensuring lvalue / temp as needed */;
for (int __i = 0; __i < __recv.Count(); __i++) {
    /* Prefer Get twice if by-value T has dtor issues — match list.h Filter note L421 */
    if (/* predicate body with x bound */ __recv.Get(__i) /* as x */ > thr)
        q.Add(__recv.Get(__i));
}
// q is List by value; same ownership invariants as Filter/Where today
```

More literally with a binder:

```c
auto q = List<int>();
for (int __i = 0; __i < nums.Count(); __i++) {
    int x = nums.Get(__i);           // or double-Get if needed for by-value class T
    if (x > thr)
        q.Add(x);                    // or Add(nums.Get(__i))
}
```

**No** `__lambda_N`, **no** `int(*)(int)`, **no** env pointer.

### 3.2 ForEach

```c
int sum = 0;
xs.ForEach((int x) => { sum += x; });

// →
for (int __i = 0; __i < xs.Count(); __i++) {
    int x = xs.Get(__i);
    sum += x;
}
```

### 3.3 Sort (careful)

`Sort` needs a comparator **many times** during Shell sort. Open-coding the
**entire** Shell sort at the call site is still Strategy A (copy loop from
`list.h` into AST with cmp body inlined). That is verbose but correct and fast.

**v1 recommendation:** support capturing Sort **or** defer Sort to v1.1 and ship
Where/Filter/Any/All/Find/ForEach first (highest value, simpler templates).

### 3.4 Map / Select

```c
int k = 10;
auto y = xs.Map((int x) => x * k);

// →
auto y = List<int>(xs.Count() > 0 ? xs.Count() : 1);
for (int __i = 0; __i < xs.Count(); __i++) {
    int x = xs.Get(__i);
    y.Add(x * k);
}
```

`Select<U>`: result type is `List<U>` with `U` from lambda return type inference
(same as today’s monomorphization of method generics).

### 3.5 Non-capturing path (must stay intact)

```c
int is_even(int x) { return (x & 1) == 0; }
auto e = xs.Where(is_even);                    // func ptr — no desugar
auto e2 = xs.Where((int x) => (x & 1) == 0);   // free-var empty → may keep today path
```

If free-var set is **empty**, keep current lowering (`static` fn + thin ptr) so
behavior and binary shape stay stable. Optional micro-opt: still open-code empty
captures for speed — not required for correctness.

---

## 4. Free-variable analysis

### 4.1 Algorithm

On a lambda AST (params + body):

1. **Bound** = parameter names (and locals declared inside the lambda body).
2. Walk body AST (`N_ID` leaves and nested scopes).
3. For each `N_ID` used as a value (not a label / type name):
   - If name ∈ bound → not free.
   - Else if resolves to **function / global / static / enum const / type** → not free.
   - Else if resolves to **automatic** binding in an enclosing function/method → **capture**.
   - Else if unresolved → normal “undeclared” error later.
4. `this` in a class method body: treat as capture of the method receiver (implicit).

### 4.2 Result

```c
typedef struct {
  node_t *ids;     /* N_ID or decl nodes of captured automatics */
  int n;
  int escapes_p;   /* v1: always 0 if we only accept HOF-literal position */
} capture_set_t;
```

### 4.3 When to run

| Lambda form | When free-var analysis runs |
|-------------|----------------------------|
| Untyped `N_LAMBDA` | At HOF call site (already in `instantiate_lambda` path) |
| Typed `(int x) =>` | **Today:** hoist at **parse** (~L6201–6211). **Change:** either delay all lambdas to check, or re-analyze after parse and **reject/reparent** if captures found |

**Required parse change (small but important):** typed capturing lambdas must **not** be irreversibly hoisted to top-level `static` at parse time if they contain free vars. Prefer:

> **All lambdas become `N_LAMBDA` (or typed-lambda-node) until check; only non-capturing ones become `static` funcs.**

That unifies paths and avoids “parse already generated a dead `__lambda_N`.”

---

## 5. Recognition of HOF call sites

### 5.1 List / Map / Set methods (UFCS or method call)

Rewrite when:

```text
receiver . Where|Filter|Map|Select|ForEach|Any|All|Find|FindOr  ( <lambda> )
```

Optional v1.1: `Sort` with capturing cmp.

Match by **method name** + arity (same soft approach as `get_seq_method` ~L13490).

Receiver types:

- `List<T>` (value or pointer; auto-deref)
- `Map<K,V>`, `Set<T>` where analogous methods exist

### 5.2 Sequence methods (arrays / slices)

Existing:

```text
seq.filter(pred) / .map(fn) / .reduce(init, fn)
```

Handled in `check_seq_method_call`. Extend capture desugar there too so
array.filter gets the same story.

### 5.3 Direct-argument only

```c
xs.Where((int x) => x > thr);     // OK
xs.Where(make_pred(thr));         // no capture desugar; make_pred returns thin ptr or error
auto p = (int x) => x > thr;      // ERROR if thr free
xs.Where(p);                      // no capture info left — p must be non-capturing fn ptr
```

---

## 6. AST rewrite recipes (check phase)

Implement as helpers next to `instantiate_lambda` (~L13698), e.g.:

```c
/* classyc.c — proposed */
static int lambda_free_vars(c2m_ctx_t, node_t lam, capture_set_t *out);
static int lambda_is_hof_arg(c2m_ctx_t, node_t call, node_t lam_arg, enum hof_kind *hk);
static node_t desugar_list_where(c2m_ctx_t, node_t call, node_t recv, node_t lam, capture_set_t *cap);
static node_t desugar_list_foreach(...);
/* ... */
```

### 6.1 Where / Filter template

Pseudocode for check rewrite of:

```c
RECV.Where(LAMBDA)   // or Filter
```

1. Typecheck `recv` → element type `T` (via List specialization / expr type).
2. Analyze LAMBDA free vars; if any free and not HOF → error.
3. If free empty → `instantiate_lambda` / existing static path; keep `Where(pred)`.
4. If free nonempty:
   - Build block (statement expression or rewrite assignment `auto q = …`):

```c
// Synthesized locals (unique names via lambda_uid or a desugar_uid)
List<T> __r = List<T>();           // or List<T>(recv.Count()) capacity hint
/* for pointer recv: use same auto-deref as method call */
for (int __i = 0; __i < recv.Count(); __i++) {
    // Bind parameters of lambda to Get(__i)
    // Inline lambda body as statements; replace `return e` in pred with
    // `if (e) __r.Add(...);`
}
// Value of desugar = __r (move)
```

**Predicate body handling:**

| Lambda body | Desugar |
|-------------|---------|
| Expr `=> e` | `if (e) __r.Add(x);` |
| Block with `return e;` only for pred | rewrite returns to `if (e) { __r.Add(x); }` control — simplest: only allow **expr-bodied** preds in v1, or single-return blocks |

**v1 simplification:** capturing HOFs that need `int`/`bool` results only allow:

- expression bodies, or  
- blocks whose every `return` is rewritten to set a `__ok` flag  

Match `list.h` Filter comment: for by-value class `T` with user dtor, **avoid** `T item = Get(i)` as a named RAII local if that still miscompiles — use double-`Get` in the desugar until that is fixed.

### 6.2 ForEach template

```c
for (int __i = 0; __i < recv.Count(); __i++) {
    T x = recv.Get(__i);   // or double-Get policy
    /* statements of lambda body; `return` becomes `continue` or disallow */
}
// type of call is void
```

### 6.3 Any / All / Find

Same loop shape; short-circuit on first success/failure; Find returns `T` or zeroed `T`.

### 6.4 Map / Select

Build result `List<U>`; `Add` mapped expr; `U` from body type.

### 6.5 Chain

```c
auto q = xs.Where((int x) => x > thr).Take(10);
```

Desugar **inner call first** (Where → temp list), then `Take` on that temp — normal left-to-right check of method chains. Each capturing HOF is independent.

---

## 7. Implementation steps (ordered)

### Phase 0 — Spec freeze & tests first (½–1 day)

- [x] This document  
- [x] Add `cy-validate/val-042-lambda-capture.cy` **expected** cases (compile + run):
  - Where with outer `int`
  - Where with outer `String` + `starts_with`
  - ForEach mutates outer sum
  - Nested Where in method using `this` field via local
  - Non-capturing regression (`classy-lambda.cy` still green)
  - **Negative:** `auto p = (int x) => x > thr;` must error with a good message
- [x] `examples/classy-docsearch.cy` uses capturing Where with local `min_score` in `search_docs`

### Phase 1 — Free-var analysis (1–2 days)

**Touch:** `src/classyc.c` near check/lambda (~L13698, `check` walkers).

- [x] `lambda_collect_free_vars(lam, &cap)`  
- [x] Classify: auto vs global vs param  
- [x] `this` treated as capture candidate  

### Phase 2 — Stop early hoist of typed lambdas (1 day)

**Touch:** `lambda_expr` ~L6198–6212, shorthand ~L6312–6327.

- [x] Typed lambdas produce `N_LAMBDA` instead of always `pending_lambdas` + `N_ID`  
- [x] Preserve typed param list for instantiate path  
- [x] Non-capturing tests still get a callable (static fn at check time)

### Phase 3 — HOF detection + desugar Where/Filter (2–3 days)

- [x] Detect `recv.Where(lam)` / `Filter` on List/Map/Set  
- [x] If free-var empty → existing thin-pointer path  
- [x] If free-var nonempty → `N_STMTEXPR` for-in open-code  
- [x] Move-only collection bind from STMTEXPR  

### Phase 4 — ForEach, Any, All, Find (1–2 days)

- [x] ForEach / Any / All templates  
- [x] Tests for mutation of outer `sum`  
- [ ] Find (deferred — zero-init edge cases)

### Phase 5 — Map / Select (1–2 days)

- [x] List.Map with capturing body (same element type)  
- [ ] Select&lt;U&gt; with capturing body (method-generic edges)

### Phase 6 — seq.filter / map / reduce (1 day)

- [x] Typed N_LAMBDA works on seq methods (non-capturing)  
- [ ] Capturing desugar for array/slice methods  

### Phase 7 — Diagnostics & docs (½ day)

- [x] Error strings for non-HOF capturing lambdas  
- [x] Update `examples/classy-lambda.cy` header  
- [x] `val-042-lambda-capture.cy`  

### Phase 8 — Sort (optional v1.1)

- [ ] Capturing Sort comparator (defer)

---

## 8. Error matrix

| Situation | Diagnostic |
|-----------|------------|
| Free local + not HOF arg | error + hint |
| Free local + HOF but subexpr context unsupported | error: “capturing Where only in simple init/statement (v1)” |
| Capture of `owned` moved-from local | existing ownership errors after desugar |
| Lambda body uses label goto out of loop | reject or limit blocks in v1 |
| Untyped lambda outside HOF | existing error ~L15772 |

---

## 9. Performance expectations

| Metric | Non-capturing today | Capturing A |
|--------|---------------------|-------------|
| Pred call | thin / possibly indirect | **inlined into loop** |
| Index RAM | N/A | **unchanged** |
| Code size | 1 static fn per lambda | 1 loop clone per call site |
| Hot filter of 1e8 elts | call overhead | **best-case C** |

At “Google shard” scale, **layout/I/O/tokenize** still dominate index build; A
removes artificial **globals + call tax** on the filter sugar layer.

---

## 10. Interaction with ownership & by-value collections

- Desugared `List<T> result` must use **`return move` / value List** same as
  `Where` today (`list.h` L442–447, house style in `BY-VALUE.md`).  
- **Do not** set `_owns_ptrs` on filter results (views stay non-owning).  
- Double-`Get` policy for by-value class elements with dtors (Filter comment
  L421–424) applies to desugar equally.  
- Capturing `owned` pointers by name: reading is fine; desugar does not extend
  lifetime — HOF finishes before frame ends.

---

## 11. Worked examples

### 11.1 Docsearch-style threshold

```c
int min_score = 1000;
auto top = hits.Where((Hit h) => h.score >= min_score);
```

Desugar sketch:

```c
int min_score = 1000;
auto top = List<Hit>();
for (int __i = 0; __i < hits.Count(); __i++) {
    Hit h = hits.Get(__i);
    if (h.score >= min_score) top.Add(h);
}
```

### 11.2 Neon-grid style

```c
int pole = cfg.pole_ms;
auto quick = samples.Where((LapSample s) => s.ms < pole);
```

### 11.3 Map Where

```c
int floor = 50;
auto hot = board.Where((String k, int v) => v >= floor);
// open-code over KeyAt/ValAt insertion order
```

### 11.4 Still thin C interop

```c
int cmp(int a, int b) { return a - b; }
xs.Sort(cmp);   // unchanged
```

---

## 12. Explicit non-goals (v1)

| Non-goal | Reason |
|----------|--------|
| Fat `{fn,env}` pointers | User constraint; C-shaped ABI |
| Returning closures from functions | Needs heap/fat |
| Nested functions / trampolines | Portability / security |
| Full C++ `[=]`/`[&]` lifetime lattice | Complexity |
| Changing `Where(int(*pred)(T))` signature | Non-capturing path stays |
| Parallel `Where` | Separate feature; A composes later |

---

## 13. Risk register

| Risk | Mitigation |
|------|------------|
| Statement-expression / value context hard | v1: only `auto x = recv.Where(lam);` and expression statements |
| By-value `T` + RAII Get bug | Mirror Filter’s double-Get |
| Method chain codegen | Desugar innermost first; re-check types |
| Code size from many clones | Accept for v1; Strategy B only if sizes hurt |
| Parse still builds unused `__lambda_N` | Phase 2: delay hoist |
| Confusion with seq.filter vs List.Where | Same capture rules; shared free-var helper |

---

## 14. Success criteria

- [x] `val-042-lambda-capture.cy` green  
- [x] `examples/classy-lambda.cy` still green (non-capturing)  
- [ ] `cy-validate` full suite green (spot-checked; run full suite after land)  
- [x] Docsearch `search_docs` uses local `min_score` + capturing Where (no g_* threshold)  
- [x] Capturing lambda assigned to variable → **hard error** with hint  
- [x] No new runtime representation (open-coded for-in; thin fn ptr only when free-var empty)  

---

## 15. Suggested code ownership map

| Step | Primary file | Functions / areas |
|------|--------------|-------------------|
| Free vars | `src/classyc.c` | new `lambda_collect_free_vars` near L13698 |
| Delay hoist | `src/classyc.c` | `lambda_expr` L6159–6212, shorthand L6312+ |
| List HOF desugar | `src/classyc.c` | method call check / UFCS (where `Where` is resolved) |
| Seq HOF | `src/classyc.c` | `check_seq_method_call` L13466+, `instantiate_lambda` L13703 |
| Tests | `cy-validate/val-0XX-*.cy` | new |
| Docs | `LAMBDA-CAPTURE.md`, `examples/classy-lambda.cy` | this file |

---

## 16. Later: path to “Google machine” without abandoning A

Per-machine pipeline (unchanged language story):

```text
[scan] → [decode/tokenize] → [Map term→postings] → [filter/rank List]
                ↑ A lives here for app-level filters
[join shards] → [view/HTML]
```

If shards need **reusable** filter kernels under weak inlining, introduce
**Strategy B only for those kernels** (monomorphized helpers + capture as plain
args). Data plane stays packed arrays / SQLite / mmap — not fat closures.

---

## 17. One-screen summary

```text
TODAY:   (int x) => x>thr   →  static __lambda(int x)  // thr invisible
                             →  Where(int(*)(T))

V1 A:    xs.Where((int x) => x>thr)
                             →  for-loop in caller; thr is normal local
                             →  no fn ptr, no env, no fat

STILL:   xs.Where(is_even)  →  thin C function pointer (unchanged)
ESCAPE:  auto f = (int x)=>x>thr  →  ERROR (use B/fat later if ever)
```

**Next action after this doc:** Phase 0 tests + Phase 1 free-var walker PR.
