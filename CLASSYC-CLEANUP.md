# ClassyC cleanup & ergonomics

Updated after List/Map throw-on-OOB, nested generics, stack collection values,
stack Map string subscript, neon-grid by-value showcase, and full `cy-validate`.

**Next product target:** first-class **by-value collection idiom** — LINQ
transforms that return RAII `List`/`Map` values so everyday code does not need
`owned auto` / `delete` on every pipeline step. See [§ First-class by-value
idiom](#first-class-by-value-idiom--next-target).

---

## What already works (keep / lean on)

| Area | Reality |
|------|---------|
| **String** | Arena + chain (`trim().upper()`), `split`/`join`, literal receivers, header builtins (`string_builtins.h`) |
| **dict / JSON** | Brace-init, `json()`, arrays, for-in, `(T)d` / `(T)?d` bind, `?.` / `??` |
| **List / Map / Set** | Full std methods; OOB **throws**; destroy on Clear/Set/Remove; `.owns()` for `T*` |
| **Stack collections** | `auto xs = List<int>();` / `Map<String,int>()` / `Set` — `~List`/`~Map` at scope exit |
| **Stack Map subscript** | `m["k"]` / `m["k"]=v` on **value** receivers (fixed: no C `i[a]` swap on `TM_CLASS`) |
| **By-value elements** | `List<LapSample>`, `Map` keys/values, `__destroy` on delete/clear |
| **Move-only containers** | Bare `List` assign is error; `move` transfers buffer |
| **owned / move / readonly** | Escapes for heap; not needed for pure stack locals |
| **Generics** | Nested List/Map, method generics `Select<U>`, free fns `Max`/`GroupBy`+UFCS |
| **nameof / typeof** | Types, enums, values — JSON dispatch + reflection |
| **Validate** | `cy-validate` green (41+ files); `examples/classy-neon-grid.cy` stack house style |

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
// ~top / ~quick free buffers at scope exit
```

---

## First-class by-value idiom — **next target**

### Why

Most developers write the **simplest** memory story first:

```c
auto top3 = grid.OrderByPace().Take(3);
for (auto p in top3) p.Banner();
// expect cleanup without thinking
```

Today that becomes:

```c
owned auto by_pace = OrderByPace(&grid);  // List* from Copy+Sort
owned auto top3    = by_pace.Take(3);     // another List*
// every LINQ step re-introduces ownership
```

That trains “collections are C owning pointers,” which fights the product
stance (C++ `vector` / RAII). **`owned` should be the heap escape hatch**, not
the default for every intermediate.

### Two ownership questions (keep separate)

| Question | By-value shell | `.owns()` on `T*` |
|----------|----------------|-------------------|
| Who frees the **list/map buffer**? | `~List` / `~Map` / `owned` delete | same |
| Who deletes **pointees**? | N/A for `List<T>` POD | only the true owner |

Views from `Where`/`Take`/`Copy` of `List<Pilot*>` must stay **non-owning** of
elements (never copy `_owns_ptrs`) — same as now.

### Target API (product)

```c
// Ideal — all local shells are values
auto grid = List<Pilot*>();
grid.owns();

auto by_pace = OrderByPace(grid);   // List by value (moved out of helper)
auto top3    = by_pace.Take(3);     // value List — RAII
auto silver  = by_pace.Skip(1).Take(1);

auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));
auto quick = samples.Where((LapSample s) => s.IsQuick());  // value List

// Escape only when you need a heap identity
owned auto shared = new List<int>();
```

Helpers ideally take / return values (or by-ref) with **move**:

```c
List<Pilot*> OrderByPace(List<Pilot*>* src) {
    auto r = List<Pilot*>();           // or: auto r = move *src.Copy() once values work
    // prefer: List by value copy API
    for (int i = 0; i < src->Count(); i++) r.Add(src->Get(i));
    r.Sort(ByPace);
    return move r;                     // move-return — not new List*
}
```

### Work packages (ordered)

#### P0 — Move return / bind for move-only collections  🔴 **blocker**

Today:

```c
List<int> make() {
    auto a = List<int>();
    a.Add(1);
    return move a;     // emit path incomplete
}
// List<int> xs = make();           // ERROR: cannot copy-initialize collection
// auto xs = move make();           // ERROR: move needs lvalue
```

**Done when:**

```c
List<int> make() {
    auto a = List<int>();
    a.Add(1); a.Add(2);
    return move a;     // or NRVO / prvalue return
}
int main() {
    auto xs = make();  // binds value; ~xs frees buffer once
    return xs.Count();
}
```

Touch: check `class_is_move_only_collection_p` paths for returns; gen move-out
of return slots; allow prvalue init of move-only type without shallow copy.

#### P1 — Value-returning transforms in `list.h` / `map.h`  🔴 **API**

Change (or dual-ship) return types:

| Method | Today | Target |
|--------|-------|--------|
| `Copy` / `Slice` / `Take` / `Skip` / `Distinct` / `Plus` | `List<T>*` | `List<T>` by value (or `List` rvalue) |
| `Filter` / `Where` / `Map` | `List<T>*` | `List<T>` |
| `Select<U>` | `List<U>*` | `List<U>` |
| `Range` / `Repeat` / factories | `List<T>*` | `List<T>` |
| Map `Where` / `SelectValues` / `Copy` | `Map*…` | value `Map` |

Implementation sketch (same for Take/Where/…):

```c
List<T> Take(int count) __attribute__((da_ignore)) {
    if (count < 0) count = 0;
    if (count > this->length) count = this->length;
    auto result = List<T>(count > 0 ? count : 4);  // stack local
    for (int i = 0; i < count; i++) result.Add(Get(i));
    return move result;  // requires P0
}
```

Keep **`List<T>*` variants** temporarily if needed for migration
(`CopyPtr` / deprecation), or bump examples only once tests are green.

**Invariant:** results remain non-owning of `T*` (do not copy `_owns_ptrs`).

#### P2 — Generics: return type `List<T>` in method bodies  🟡

Specialisation already supports nested generics. Returning `List<T>` by value
from monomorphized methods must:

* size call-arg / return ABI for nested List  
* not allocate via `new` unless escaping  

Tests: monomorphized `Where` on stack + heap receivers; `List<LapSample>` +
`List<int>` + `List<Pilot*>`.

#### P3 — Call-site sugar (after P0–P1)  🟢

```c
auto q = samples.Where(...).Take(10);   // chain of values
auto m = board.Where(...);              // value Map filter
```

Docs/examples: `classy-neon-grid.cy`, README, BY-VALUE.md — strip `owned` from
local pipelines.

### Explicit non-goals (for this target)

* GC / refcount on collections  
* Implicit deep copy of `List`  
* Views that steal `.owns()` from the source  

### Exit criteria

- [x] `return move list;` + `auto x = f();` for `List`/`Map`/`Set`  
- [x] `Take`/`Skip`/`Where`/`Copy` return values in headers (or dual API)  
- [x] `examples/classy-neon-grid.cy` podium without `owned` on Take/Skip  
- [x] `cy-validate` + new `val-040-value-transforms.cy`  
- [x] README house style: stack first, `owned` for escape  

---

## Done backlog (compressed)

* List OOB throws; destroy on mutate; owns; Where/Select/Range/Plus; GroupBy free+UFCS  
* Map TryAdd, Get throws KeyException, SelectValues/Keys/GroupBy, JSON keys for int  
* Nested generics + method generics + nameof/typeof  
* Stack List/Map/Set + move-only assign  
* Stack Map string subscript (N_IND: do not swap `TM_CLASS` with pointer key)  
* Method frame layout: no FUNC_DEF on FDA; don’t treat N_CLASS as stack frame  
* Enum bare names via tpname + S_TAG only (no S_REGULAR tag attr crash on `typedef enum E E`)  
* test-list-stdlib OOB expects throws; neon-grid stack house style  

### Not fixed / intentional

| Item | Notes |
|------|--------|
| Language `list[1..3]`, `operator+` | Still sugar; use Slice / Plus |
| `new List<int>(4)` capacity vs `{4}` singleton | C# parity |
| Method forward-order in class | Declare callees first |
| `Select<U>` on some stack+value-T sites | Workaround: open code or explicit heap intermediate |
| Uncaught exception → abort/core | UX; prefer exit(1) + message |

---

## Prioritized roadmap (current)

1. ~~**P0** Move-return of move-only collections~~ **done**  
2. ~~**P1** Value-returning List/Map transforms~~ **done**  
3. ~~**P2** Specialisation coverage for value returns~~ **done** (method body complete-self type)  
4. ~~**P3** Examples/docs + validate suite~~ **done** (`val-040`, neon-grid, aurora-ops)  
5. Later: dual `WherePtr` if migration needs it; slice/`+` sugar; `if constexpr`;
   full-expr temp dtor for pure rvalue chains; GroupBy as value Map;
   Select edge cases on all stack value-T receivers; ownership-pass speed  

---

## Notes for implementers

* Transforms that `new List` force `owned`/delete — that’s the gap for the idiom.  
* `List.owns()` for domain `T*` is orthogonal; value returns must **never** copy owns onto views.  
* ABI: class by-value pass/return + frame/ALLOCA sizes (12-byte class → sizeof 16 with min align 8).  
* Do not reintroduce enum tag as `S_REGULAR` def_node (crashes `def_symbol` on typedef same name).  
* `map.h` comments: never write `*/` inside `/* … */` (breaks `Select*/Copy`-style prose).  

Related: [`BY-VALUE.md`](BY-VALUE.md), [`GENERICSMEM.md`](GENERICSMEM.md),
[`CLASSYC-FINDINGS.md`](CLASSYC-FINDINGS.md), `examples/classy-neon-grid.cy`,
`cy-validate/val-038-*.cy`, `val-039-*.cy`.
