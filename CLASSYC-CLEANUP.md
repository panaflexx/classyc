# ClassyC cleanup & ergonomics

Updated 2026-07-16 after List/Map throw-on-OOB, nested generics, stack collection
values, value-returning LINQ transforms, move-return / prvalue bind, GetMut /
`[]` lvalues, capturing HOF lambdas (**Find**, **Sort**, **Select**), try/argv,
stack **Select** monomorph, uncaught **exit(1)**, shift-range guards, **GroupBy
Phase B** (`Map<G, List<V>>` nested List shells), true `List<List<T>>`, dense
`*(data+i)` + memcpy growth, `List*[i]` Get/Set sugar restored, quiet scalar/POD
`move` monomorph, neon-grid / aurora-ops showcases, and full `cy-validate`
(**52** `val-*.cy` files).

**Product target (by-value collection idiom):** **landed.** Everyday LINQ
pipelines use RAII `List`/`Map`/`Set` values — no `owned auto` / `delete` on
every step (including GroupBy’s nested `Map<G, List<V>>`). See [§ First-class
by-value idiom](#first-class-by-value-idiom--landed).

---

## What already works (keep / lean on)

| Area | Reality |
|------|---------|
| **String** | Arena + chain (`trim().upper()`), `split`/`join`, literal receivers, header builtins (`string_builtins.h`) |
| **dict / JSON** | Brace-init, `json()`, arrays, for-in, `(T)d` / `(T)?d` bind, `?.` / `??` |
| **List / Map / Set** | Full std methods; OOB **throws**; destroy on Clear/Set/Remove; `.owns()` for `T*` |
| **Stack collections** | `auto xs = List<int>();` / `Map<String,int>()` / `Set` — `~List`/`~Map`/`~Set` at scope exit |
| **Value-returning transforms** | `Where`/`Take`/`Skip`/`Copy`/`Slice`/`Plus`/`Distinct`/`Map`/`Filter`/`Select<U>` / Map `Where`/`Keys`/`Values` / Set `Filter`/`Union` → **by value** |
| **GroupBy (Phase B)** | Value `Map<G, List<V>>` — nested List shells in map dense buffer (val-049, val-050) |
| **Move return / bind** | `return a;` / `return move a;` of move-only collections; `auto x = f()` prvalue bind |
| **Stack Map subscript** | `m["k"]` / `m["k"]=v` on **value** receivers (N_IND does not swap `TM_CLASS` with pointer key) |
| **GetMut / `[]` lvalue** | `list[i].Method()` / `map[k].Method()` mutates buffer; **value and pointer** receivers use Get/Set sugar (`list_ptr[i]` too) |
| **By-value elements** | `List<LapSample>`, `Map` keys/values, `__destroy` on delete/clear |
| **Move-only containers** | Bare `List` assign is error; `move` transfers buffer |
| **Capturing HOF lambdas** | Direct arg to `Where`/`Filter`/`Map`/`ForEach`/`Any`/`All`/`Find`/`Sort`/`Select` open-coded; non-capturing stay thin fn ptrs (val-042 §1–12) |
| **Stack Select** | Pure stack `List<T>` method generics (no prior `List*` needed); val-046 + capturing Select |
| **owned / move / readonly** | Escapes for heap; not needed for pure stack locals / value pipelines |
| **Generics** | Nested type names / method generics `Select<U>`, free fns `Max`/`GroupBy`+UFCS |
| **nameof / typeof** | Types, enums, values — JSON dispatch + reflection |
| **try + argv** | Adjusted array params survive `force_val` under try (val-045) |
| **Uncaught exceptions** | Print + **`exit(1)`** (not abort/core); optional `CY_EXC_ABORT` (val-047) |
| **Shift-range safety** | Negative / ≥width shift → catchable trap (val-048, bugs/009) |
| **Validate** | `cy-validate` **52** files (incl. val-050 nested collections); `bugs/run-bugs.sh`; showcases neon-grid, aurora-ops |

### House style today (what you should write)

```c
// Domain pointers — one owner
auto grid = List<Pilot*>();
grid.owns();
grid.Add(new Pilot(...));

// DTO by value — stack list, no owns
auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));

// Stack map + string subscript
auto board = Map<String, int>();
board["AURORA"] = 2165;
int e = board["AURORA"];

// Value-returning transforms — RAII shells, no owned/delete
auto quick = samples.Where((LapSample s) => s.IsQuick());
auto top   = quick.Take(3);
auto ms    = samples.Select((LapSample s) => s.ms);  // stack Select OK
// ~top / ~quick / ~ms free buffers at scope exit

// Capturing predicates / projectors / comparators (direct HOF arg)
int thr = 3;
auto big = nums.Where((int x) => x > thr);
int want = 5;
Ship s = fleet.Find((Ship x) => x.id == want);
int flip = -1;
nums.Sort((int a, int b) => flip * (a - b));
int mul = 10;
auto scaled = nums.Select<int>((int x) => x * mul);

// GroupBy — Map of List shells (value nested collections)
auto by = roster.GroupBy((Ship s) => s.SectorKey());
for (auto k, bucket in by)
    printf("%d: %d\n", k, bucket.Count());
// ~by frees map + every nested List

// In-place mutation of by-value class elements
fleet[0].Boost(5);                // GetMut lvalue — not Get() copy
// fleet.Get(0).Boost(5);         // WRONG: copy; Boost is lost
// Heap List* uses the same [] sugar:
List<int>* p = new List<int>();
p[0] = 42;                        // → Set; also (*p)[0] / p->Get(0)

// Helpers return values
List<Pilot*> OrderByPace(List<Pilot*>* src) {
    auto r = src.Copy();
    r.Sort(ByPace);
    return r;                     // implicit move
}
auto top3 = OrderByPace(&grid).Take(3);
```

---

## First-class by-value idiom — **landed**

### Why (history)

Most developers write the **simplest** memory story first:

```c
auto top3 = grid.OrderByPace().Take(3);
for (auto p in top3) p.Banner();
// expect cleanup without thinking
```

Before this work that became:

```c
owned auto by_pace = OrderByPace(&grid);  // List* from Copy+Sort
owned auto top3    = by_pace.Take(3);     // another List*
// every LINQ step re-introduced ownership
```

That trained “collections are C owning pointers,” which fought the product
stance (C++ `vector` / RAII). **`owned` is the heap escape hatch**, not the
default for every intermediate.

### Two ownership questions (keep separate)

| Question | By-value shell | `.owns()` on `T*` |
|----------|----------------|-------------------|
| Who frees the **list/map buffer**? | `~List` / `~Map` / `owned` delete | same |
| Who deletes **pointees**? | N/A for `List<T>` POD | only the true owner |

Views from `Where`/`Take`/`Copy` of `List<Pilot*>` stay **non-owning** of
elements (never copy `_owns_ptrs`) — same as now. GroupBy **buckets** are
nested **value** `List<V>` shells stored in the Map dense buffer (Phase B);
`~Map` destroys every bucket List.

### Target API (product) — implemented

```c
// Ideal — all local shells are values  ✅
auto grid = List<Pilot*>();
grid.owns();

auto by_pace = OrderByPace(&grid);  // List by value (moved out of helper)
auto top3    = by_pace.Take(3);     // value List — RAII
auto silver  = by_pace.Skip(1).Take(1);

auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));
auto quick = samples.Where((LapSample s) => s.IsQuick());  // value List
auto ms    = samples.Select((LapSample s) => s.ms);        // stack Select

auto by = roster.GroupBy(keyFn);   // value Map — no owned

// Escape only when you need a heap identity
owned auto shared = new List<int>();
```

Helpers take / return values (or by-ref) with **move**:

```c
List<Pilot*> OrderByPace(List<Pilot*>* src) {
    auto r = src.Copy();
    r.Sort(ByPace);
    return r;                     // or `return move r;` — both work
}
```

### Work packages (status)

#### P0 — Move return / bind for move-only collections  ✅ **done**

```c
List<int> make() {
    auto a = List<int>();
    a.Add(1); a.Add(2);
    return a;           // implicit move (or explicit `return move a`)
}
int main() {
    auto xs = make();   // prvalue bind; ~xs frees buffer once
    return xs.Count();
}
```

Compiler: `class_is_move_only_collection_p`; N_RETURN implicit `N_MOVE`;
`create_decl` `prvalue_init_p` for `N_CALL` / `N_STMTEXPR`. Tests: val-040 §1.

#### P1 — Value-returning transforms in `list.h` / `map.h` / `set.h`  ✅ **done**

| Method | Return type |
|--------|-------------|
| `Copy` / `Slice` / `Take` / `Skip` / `Distinct` / `Plus` | `List<T>` by value |
| `Filter` / `Where` / `Map` | `List<T>` |
| `Select<U>` | `List<U>` |
| `Range` / `Repeat` | `List<T>` |
| Map `Where` / `SelectValues` / `Copy` / `Keys` / `Values` | value `Map` / `List` |
| Map / List `GroupBy` | value `Map<G, List<…>>` (nested List shells) |
| Set `Filter` / `Union` / `Intersect` / `Difference` | value `Set` |

**Invariant:** results remain non-owning of `T*` (do not copy `_owns_ptrs`).

#### P2 — Generics: return type `List<T>` in method bodies  ✅ **done**

Specialisation supports nested type names; monomorphized methods return
complete-self / nested `List` by value for ordinary element types. Tests:
val-040, val-031, **val-046**. True nested collections **landed (Phase B):**
`List<List<T>>`, `Map<G, List<V>>`, GroupBy → nested List shells (val-049,
val-050). Dense buffers use `*(data+i)` + `memcpy` growth (SSA/MIR-friendly);
user-facing `List*[i]` stays Get/Set sugar.

#### P3 — Call-site sugar + docs  ✅ **done**

```c
auto q = samples.Where(...).Take(10);   // chain of values
auto m = board.Where(...);              // value Map filter
auto g = roster.GroupBy(...);           // value Map filter buckets
```

Docs/examples: `classy-neon-grid.cy`, `classy-aurora-ops.cy`, README house
style, `val-040`…`val-050`.

### Explicit non-goals (still)

* GC / refcount on collections  
* Implicit deep copy of `List`  
* Views that steal `.owns()` from the source  

### Exit criteria

- [x] `return move list;` / implicit `return list;` + `auto x = f();` for `List`/`Map`/`Set`  
- [x] `Take`/`Skip`/`Where`/`Copy` return values in headers  
- [x] `examples/classy-neon-grid.cy` podium without `owned` on Take/Skip  
- [x] `cy-validate` + `val-040-value-transforms.cy`  
- [x] README house style: stack first, `owned` for escape  
- [x] Stack `Select` without prior `List*` (val-046)  
- [x] GroupBy without `owned` (val-049)  
- [x] Capturing `Find` / **`Sort`** / **`Select`** (val-042 §10–12)  
- [x] Uncaught → exit(1); shift-range; bugs runner  
- [x] `List*[i]` / `map_ptr[k]` Get/Set sugar (developer expectation)  
- [x] True nested `Map<G, List<V>>` / `List<List<T>>` (Phase B; val-050)  

---

## After the by-value target (remaining)

| Item | Notes |
|------|--------|
| **Full-expr temp dtor** | Pure `f().Where(...).Take(3)` without names — intermediate RAII may need full-expr temps |
| **Language sugar** | `list[1..3]`, `operator+` — still use Slice / Plus |
| **`new List<int>(4)` capacity vs `{4}` singleton** | C# parity |
| **Method forward-order in class** | Declare callees first |
| **AOT DCE / ownership speed** | See FINDINGS §§4–5 |
| **Stale bugs/** | 002 retire/self-check; 003 update expectations |
| **Array `.filter`/`.map`** | Deferred; collection HOF capture already covers List/Map/Set |

### Compiler notes landed with capture / stmtexpr (2026-07-16)

* **`move` on scalars** is a no-op pass-through (does not I64-zero int slots — that
  corrupted `EnsureCapacity` / growth for `List<int>`).
* **Capturing HOF stmtexpr results** (class-valued) reserve space in the
  **call-arg area**, not `func_block_scope->size` local offsets — avoids
  overwriting the source `List` and double-free on scope exit.
* **N_ASSIGN / N_RETURN storage → `.Copy()`** for move-only collections: unlink
  the RHS/return expr **before** grafting it under `Copy()` (DLIST ownership).
* **`[]` protocol:** value *and* pointer collection receivers use Get/Set;
  plain class pointers without Get stay C raw index.

---

## Done backlog (compressed)

* List OOB throws; destroy on mutate; owns; Where/Select/Range/Plus; GroupBy free+UFCS  
* Map TryAdd, Get throws KeyException, SelectValues/Keys/GroupBy, JSON keys for int  
* Nested generics + method generics + nameof/typeof  
* Stack List/Map/Set + move-only assign  
* Stack Map string subscript (N_IND: do not swap `TM_CLASS` with pointer key)  
* Method frame layout: no FUNC_DEF on FDA; don’t treat N_CLASS as stack frame  
* Enum bare names via tpname + S_TAG only (no S_REGULAR tag attr crash on `typedef enum E E`)  
* **P0–P3 by-value idiom** (move return, value transforms, specialisation, docs)  
* GetMut / FirstMut / LastMut / ValMut + `[]` → lvalue for by-value class elements  
* Capturing lambdas Strategy A (open-code HOF args) **including Find, Sort, Select**  
* try + adjusted array params (`force_val` pointer load, not `&argv`)  
* **Stack Select monomorph** — `uniq_cstr` on expr-context generic name + specs `orig_name`  
* **Uncaught exception / safety trap → exit(1)** (`cyexc.h`; optional `CY_EXC_ABORT`)  
* **Shift-range guard** (`_safety_trap` reason 5)  
* **GroupBy Phase B** — value `Map<G, List<V>>` nested List shells (no `owned` at call site)  
* **Nested `List<List<T>>` / `Map<G,List<V>>`** — dense `*(data+i)`, memcpy growth, mir-gen addr-elim  
* **bugs/run-bugs.sh**  
* **`List*[i]` Get/Set sugar restored** (not raw C array of Lists)  
* Quiet scalar/POD `move` monomorph (intentional no-ops; no warning spam)  
* test-list-stdlib OOB expects throws; neon-grid / aurora-ops stack house style  

---

## Prioritized roadmap (current)

1. ~~**P0** Move-return of move-only collections~~ **done**  
2. ~~**P1** Value-returning List/Map transforms~~ **done**  
3. ~~**P2** Specialisation coverage for value returns~~ **done**  
4. ~~**P3** Examples/docs + validate suite~~ **done**  
5. ~~**Stack Select / uncaught exit / shift / Find capture / GroupBy shell / bugs runner**~~ **done**  
6. ~~**Capturing Sort + Select open-code**~~ **done** (val-042 §11–12)  
7. ~~**`List*[i]` developer sugar**~~ **done**  
8. ~~**Nested `Map<G,List<V>>` / `List<List<T>>`**~~ **done** (val-050)  
9. **Next:** full-expr temp dtor; ownership-pass speed; AOT DCE; retire stale
   bugs; array `.filter`/`.map`; `Heap` priority queue if needed

---

## Notes for implementers

* Transforms that still `new List`/`new Map` force `owned`/delete — today that is
  mainly **compat helpers** (`SelectString`, static `FromJson` heap return).
  GroupBy buckets are nested **value** Lists.  
* `List.owns()` for domain `T*` is orthogonal; value returns must **never** copy
  owns onto views (val-040 §7).  
* ABI: class by-value pass/return + frame/ALLOCA sizes (12-byte class → sizeof 16
  with min align 8).  
* Do not reintroduce enum tag as `S_REGULAR` def_node (crashes `def_symbol` on
  typedef same name).  
* Generic method monomorph needs a **stable** `generic_specs.orig_name` — never
  store a stack buffer (`g_name_buf`); always `uniq_cstr` (val-046).  
* Synthetic lambda bodies for open-code / method generics must be **N_BLOCK**
  (`{ return e; }`), never a bare expr — `N_FUNC_DEF` uses the body as the
  function scope (capturing Find seed).  
* `map.h` comments: never write `*/` inside `/* … */` (breaks `Select*/Copy`-style
  prose).  
* Adjusted array params under try: `force_val` must load the pointer value when
  `type->mode == TM_PTR` even if `arr_type` is set (val-045).  
* Nested collections: keep **user** `List*[i]` as Get/Set; use `*(data + i)` (or
  equivalent) for dense `T*` slots inside monomorphized headers. Do not re-break
  pointer sugar to unblock nested List-of-List.  

Related: [`BY-VALUE.md`](BY-VALUE.md), [`GENERICSMEM.md`](GENERICSMEM.md),
[`LAMBDA-CAPTURE.md`](LAMBDA-CAPTURE.md), [`CLASSYC-FINDINGS.md`](CLASSYC-FINDINGS.md),
[`sketch/OPEN-ISSUES-PRIORITY.md`](sketch/OPEN-ISSUES-PRIORITY.md),
`examples/classy-neon-grid.cy`, `examples/classy-aurora-ops.cy`,
`examples/test-generic-ptr-args.cy`,
`cy-validate/val-038-*.cy` … `val-050-*.cy`, `bugs/run-bugs.sh`.
