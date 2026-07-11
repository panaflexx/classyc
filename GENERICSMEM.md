# Generics, Memory & the Collection Protocol in ClassyC

How `List<T>` / `Set<T>` / `Map<K,V>` store elements, what `T` may be, stack
vs heap shells, and the duck-typed protocols that drive `for-in` and `coll[i]`.

User guide + implementer map. Product stance (C++ vector / RAII) and the
**next target** (value-returning transforms) are in [`BY-VALUE.md`](BY-VALUE.md)
and [`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md).

---

## TL;DR

| Element type `T` | List | Set | Map K/V | Notes |
|------------------|:----:|:---:|:-------:|-------|
| scalars | ✅ | ✅ | ✅ | inline |
| `String` | ✅ | ✅ | ✅ | content hash/eq for keys |
| pointers / `MyClass*` | ✅ | ✅ | ✅ | identity; optional `.owns()` |
| **by-value class** | ✅ | ✅ | ✅ | inline + `__destroy` on Clear/delete |

| Shell (the collection object) | Status |
|-------------------------------|--------|
| Stack `auto xs = List<T>();` | ✅ RAII `~List` |
| Stack `Map` / `Set` | ✅ |
| Heap `owned auto` / `new` | ✅ |
| Move transfer `b = move a` | ✅ |
| Bare assign `b = a` | ❌ compile error |
| `Where`/`Take`/`Copy` return type | ✅ **value `List`/`Map`/`Set`** (RAII); GroupBy stays heap `Map*` |

### Preferred house style

```c
// Domain identity — owning list of pointers
auto grid = List<Pilot*>();
grid.owns();
grid.Add(new Pilot(...));

// DTOs — by-value elements, stack list
auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));

// Stack map + string subscript (value receiver)
auto board = Map<String, int>();
board["AURORA"] = 2165;

// Transforms: value shells — no owned/delete on local pipelines
auto quick = samples.Where((LapSample s) => s.IsQuick());
auto top   = quick.Take(3);
```

Rule of thumb:

* **Shell** — prefer stack value; `owned`/`new` when it must escape.  
* **Elements** — POD/DTO by value; domain objects as `T*` with **one** `.owns()` owner.  
* **Views** — never copy `_owns_ptrs` onto Where/Copy/Take results.

---

## Two storage modes (elements)

A `List<T>` buffer is `sizeof(T) * capacity`.

1. **Scalars / String / raw pointers / `MyClass*`** — slot holds a machine value;
   Add copies bits (pointer identity for objects).
2. **By-value `MyClass`** — full object inline; collection owns elements and runs
   `__destroy` (user dtor if any) on Clear / Set overwrite / RemoveAt / `~List`.

Prefer **no user dtor** (or only quiet counters) on list-element DTOs if the type
is bitwise relocated; use `List<T*>.owns()` when `T` needs a real destructor.

---

## Collection protocols

### Indexed: `Count()` / `Get(int)` / `Set(int,T)`

```c
for (auto x in coll)       // Get(i)
for (auto i, x in coll)    // index + element
coll[i]                    // Get
coll[i] = v                // Set
```

Works for **value or pointer** receivers (`.` auto-deref).

`Get` may return by-value class types (for-in block-copies into the loop var).

### Keyed (Map): `Count()` / `KeyAt` / `ValAt` + `Get(K)` / `Set(K,V)`

```c
for (auto k in map)
for (auto k, v in map)
map[k]          // Get(K) — throws KeyException if missing
map[k] = v      // Set
```

**Stack Map + non-integer keys:** supported. Compiler must **not** apply C’s
`i[a]`/`a[i]` operand swap when the primary is `TM_CLASS` (otherwise
`m["k"]` becomes `"k"[m]`). Fixed in `check` `N_IND`.

---

## Set / Map hashing

* `String` keys/elements: content (FNV / strcmp).  
* Everything else: raw bytes (scalars by value, pointers by identity).

---

## Generics (specialisation)

Concrete scalars, String, pointers (`P` mangling), by-value classes, nested
`List` inside Map, method generics (`Select<U>`), free generic fns (`Max`,
`GroupBy` + UFCS) are supported for the std headers path.

Still open: full `if constexpr` / `is_int<T>`; some method-generic sites on
stack+value-T need workarounds; value-returning monomorphized methods (next
target).

---

## By-value class elements

### ABI

Classes pass/return like structs (`TM_CLASS` in x86_64 ABI helpers).  
Watch: min class align is 8 → `sizeof` of 3×`int` is **16**, not 12.

### Equality

`==` / `!=` on class values → memcmp (padding participates).

### Destruction

```c
~List() {
    for (int i = 0; i < length; i++) {
        if (_owns_ptrs && is_pointer<T>()) delete data[i];
        else __destroy(data[i]);
    }
    free(data);
}
```

Same idea in `~Set` / `~Map` (Map destroys keys **and** values).

### Shell construction

```c
List<int> a;
auto b = List<int>();
auto c = List<int>(16);
auto m = Map<String, int>();
auto s = Set<String>();

owned auto h = new List<int>();   // when you need a pointer / heap lifetime
```

Brace-init heap:

```c
owned auto xs = new List<Pt>{ Pt(1, 2), Pt(3, 4) };
```

Stack DTO pipeline (elements by value):

```c
auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));
owned auto quick = samples.Where((LapSample s) => s.IsQuick());  // shell is heap List* today
```

### Move-only shells

```c
auto a = List<int>();
a.Add(1);
auto b = List<int>();
b = move a;     // OK — a emptied
// b = a;       // ERROR — would double-free
```

---

## Transform results

| API | Status |
|-----|--------|
| `Where` / `Filter` / `Take` / `Skip` / `Copy` / `Slice` / `Plus` / `Distinct` | ✅ `List<T>` by value + move return |
| Map `Where` / `SelectValues` / `Copy` / `Keys` / `Values` | ✅ value `Map` / `List` |
| Set `Union` / `Intersect` / `Filter` | ✅ value `Set` |
| `GroupBy` | heap `Map*` (ownsValues buckets) — use `owned auto` |
| Local pipeline | `auto r = xs.Take(3);` RAII |

```c
auto by_pace = OrderByPace(&grid);   // returns List by value
auto top3    = by_pace.Take(3);      // value shell; Pilot* non-owning
// grid.owns() still owns the pilots
```

---

## Still open

* Value-returning transforms + move return (**product next**)  
* Member-wise `==` for classes with padding / intentional identity  
* Slice syntax / operator+ language sugar  
* Stronger Select monomorphization on all stack value-T receivers  

---

## File / symbol index

| Area | Where |
|------|-------|
| Specialisation / placeholders | `get_or_create_specialization`, `specialize_node` |
| Move-only collection check | `class_is_move_only_collection_p` |
| Subscript N_IND (class, no swap) | `check` `case N_IND` |
| Subscript assign → Set | `gen` `case N_ASSIGN` |
| for-in | `check`/`gen` `N_FORIN` |
| Frame layout for methods | `process_func_decls_for_allocation` (stop at N_CLASS) |
| `__destroy` / owns | `list.h` / `map.h` / `set.h` |
| Showcase | `examples/classy-neon-grid.cy` |
| Validate | `val-015`, `val-038`, `val-039`, `val-028` |

Related: [`BY-VALUE.md`](BY-VALUE.md), [`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md),
[`CLASSYC-FINDINGS.md`](CLASSYC-FINDINGS.md).
