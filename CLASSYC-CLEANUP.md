In ClassyC today: `String` is great, `defer delete` is noisy, `List`/`Map` work but are still very C#-2005 while `dict` does the JSON heavy lifting. Here's an ergonomic audit — updated after List fixes, nested/concrete generic specialisation, and Map follow-ups.

### What you already nailed
* `String` arena + `trim().upper()` chaining, `split/join`, literal receiver `"MiXeD".lower()` — feels like Python/C#.
* **Header-extensible builtins** via `[[builtin_method(...)]]` (`include/string_builtins.h`): String methods are a data table + attribute registration, not a frozen `if/else` in the compiler. Add `s.upcase()`-style aliases or new `c2m_str_*` bindings from a header without new `SM_*` cases (stock methods keep special codegen; new ones use `SM_EXT`).
* `dict` heterogeneous + `json()`, `d.items[0].name`, `for (auto k,v in dict)` + typed `for (String s in d.tags)` is nicer than any C JSON lib.
* `(User) d` / `(User)? d` binding is unique-selling-point.
* `List<T>{1,2,3}`, `Filter/Map`, `Map[k]=v`, `for (auto k,v in map)` duck-typing, `GetOr/TryGet` throwing `KeyException` (`bugs/006`) is correct direction.
* `owned / move / readonly` is miles ahead of manual `defer delete`.
* **Nested / concrete generics** (compiler): `List<K>` / `List<V>` inside `Map`, `List<String>` inside `List`, `List<T*>` inside `Repository<T>`, `Is<T>` inside `As<T>` — placeholders + deferred materialisation.
* **`nameof` / `typeof` reflection** (compiler): type-level `nameof<T>()` / `typeof<T>()` (typeof keeps `*`), value-level `expr.nameof()` / `expr.typeof()`, enum reverse-map (`apple.nameof()`, `f.nameof()`), free `nameof(id)`. Powers List/Map JSON dispatch + `Is<T>`/`As<T>`. `val-030`, full suite 32/32.
* Full `cy-validate` green after nested generics + Map/List cleanup + nameof/typeof.

### Compiler: concrete types & nested generics

#### Status (post-fix)
Concrete type arguments specialise correctly for the supported set:

| Arg kind | Example | Status |
|----------|---------|--------|
| Scalars | `List<int>`, `Map<int,String>` | ✅ |
| `String` / `dict` / `bool` | `List<String>`, `Map<String,int>` | ✅ |
| Pointers | `List<char*>`, `List<User*>`, `Map<String,Track*>` | ✅ (pointer fixup + mangle `P`) |
| By-value class | `List<MyClass>`, `Map<int,Item>` | ✅ ABI + `__destroy` |
| Nested generic + param | `List<K>` in `Map`, `List<T*>` in `Repository` | ✅ placeholder → rewrite on outer specialize |
| Nested generic + concrete | `List<String>` in `List.SelectString` | ✅ deferred until outer `class_node` ready |
| Multi-param self-ref | `Map<K,V>* Copy()` | ✅ multi-token placeholder resolve |
| Generic functions | `Max(3,5)` infers `T` | ✅ (`val-023`) |

#### What was fixed in `classyc.c`
`get_or_create_specialization` while parsing a generic body:

1. **Any** type arg that is a current outer type param (self *or* cross, with pointer depth) → mangled placeholder only (`__generic_List_K`, `__generic_List_TP`). No early materialise with unresolved `K`.
2. **Fully concrete** nested types (e.g. `List<String>` inside `List<T>`) → placeholder + push `generic_crossrefs`.
3. After outer template `class_node` is back-filled → drain crossrefs and materialise real specialisations.
4. `specialize_node` already rewrites param placeholders when the outer is specialised and queues cross-generic materialisation.

#### Remaining compiler gaps on “concrete types”
These still fail or are incomplete — not the common `List`/`Map` path:

| Gap | Why |
|-----|-----|
| **Generic method type params** (`Select<U>(U(*fn)(T))`) | No per-method type params / return substitution yet |
| **`if constexpr` / `is_int<T>`** | Generic body typechecks for *all* `T`; blocks `List.Range` |
| **Generic call inference for classes** | Works for free funcs; some method sites still need explicit `<T>` |
| **Nested generic of nested generic** depth edge cases | Not exhaustively stressed beyond `List`/`Map`/`Repository` |
| **Ownership analyzer loop false UAF** | `for { h=new; use; delete h; }` may flag use-after-free on back-edge; use `unowned` (val-014) |

Concrete primitives, pointers, by-value classes, and nested collection specialisations used by std headers are **good**. Missing pieces are mostly *generic methods* and *type-conditional bodies*, not plain concrete instantiation.

### `list.h` — biggest paper cuts

1.  **Silent no-ops are footguns.** …
    * **FIXED** — `Get`/`First`/`Last`/`Pop`/`RemoveAt`/`Set` throw `OutOfBoundsException`. See `val-028` 1a-1h.

2.  **Two `owns()` signatures.** …
    * **FIXED** — `owns()` / `owns(int v)` chainable. `val-028` 4a-4b.

3.  **`ToDict()` naming.** …
    * **FIXED (compat retained)** — prefer `ToJsonArray()` / `ToJson()`; `ToString()`/`to_string()` aliases. `val-028` 5o-5p.

4.  **Memory contract inconsistent.** …
    * **FIXED / CLARIFIED** — `ToArray()` is `malloc`/`free`; element-wise assign. `val-028` 6a-6c.

5.  **Leak on mutation.** …
    * **FIXED** — `Clear`/`RemoveAt`/`Set` destroy via `is_pointer` + `__destroy`. `val-028` 3a-3f.

6.  **Missing C# / Python essentials:**
    * **FIXED partially:** `Where`/`Select` (`T→T`), `Any`/`All`/`Find*`/`AddRange`/`InsertRange`/`Distinct`/`Repeat`.
    * **NOT FIXED:** generic `Select<U>`, `GroupBy`, slice `list[1..3]`, `operator+`, `Range` factory (needs `is_int<T>`).

7.  **Constructor confusion:** capacity vs singleton for `int`.
    * **NOT FIXED — intentional** C# semantics: `new List<int>(4)` capacity, `new List<int>{4}` singleton.

### `map.h` — similar

* **FIXED:** `Contains` → `bool`; `ContainsKey` alias.
* **FIXED:** `TryAdd(K,V) -> bool` (insert-if-absent). `Set` still returns 0/1 updated-vs-inserted.
* **FIXED:** `to_string()` / `ToString()` delegate to `ToJson()` (works for `Map<String, scalar>`).
* **FIXED:** `ToDict()` guards non-String `K` with `nameof<K>()` (returns empty object; no more segfault on `Map<int,*>`). Document: JSON export needs String keys.
* **FIXED:** `Remove` / `Clear` / `Set` overwrite destroy key/value via `is_pointer` + `__destroy` (mirrors List).
* **FIXED:** `KeyAt`/`ValAt` throw `OutOfBoundsException`.
* **FIXED:** `Keys()`/`Values()` compile (nested generic) — caller still `delete`/`owned`.
* **FIXED:** `classy-map.cy` + `val-006` updated for throwing `Get` (use `GetOr` / `TryGet` / try-catch).
* **NOT FIXED:** `GetOrAdd`/`Where`/`SelectValues`/`GroupBy`-style Map API; non-String key JSON encoding.

### `dict` — you have JSON-like but not JS-like

* Safe nav `?.`, nullish `??`, spread `...`, array literals in dict: **NOT FIXED** (parser).
* Prefer `v.json` over free `json(v)` in docs.

### Static methods & Object model

* Static factories `List<T>.FromJson` / `Repeat` work. `Range` blocked by all-`T` body check.
* Method order still matters (call only methods declared earlier in same class).
* `static const` fields / extension methods: **NOT FIXED**.

### Language sugar

`scoped`, `..` range, slice syntax, `?.`/`??`, properties, implicit `"a"+5` concat, list literals without `new`: **NOT FIXED** (parser / resolution).

Generic free-function inference (`Max(3,5)`): **works** (`val-023`). Mark language-sugar item 9 partially fixed.

### Code cleanliness

* List: `ToArray`/`Equals`/`Concat`/`AddRange` index-based accessors — **FIXED**.
* Map: `to_string` → delegate to `ToJson`; `ToString()` alias — **FIXED**.
* Ownership loop false UAF — noted under compiler gaps.

### Prioritized roadmap

**Done (List + nested generics):**
* List destroy/throw/owns/`Where`/`Select`/`AddRange`/etc. — `val-028` 45 tests.
* Nested/concrete specialisation in `classyc.c` — full `cy-validate` green; `Map.Keys`/`Values`; README `Repository<T>`.

**Done (Map ergonomics):** `ContainsKey`, `TryAdd`, destroy on mutate, `to_string`/`ToString` via `ToJson`, bounds on KeyAt/ValAt, example + val-006 updated. Nested generics + full cy-validate green.

**Done (attribute builtins):** `[[builtin_method(type, method, rt, nargs, retkind[, "static"])]]` registry; stock String methods seeded + redeclared in `string_builtins.h`; `SM_EXT` generic lower for header-only additions.

**Done (nameof / typeof reflection):**
* `nameof<T>()` strips pointers (`nameof<int*>() == "int"`) — keeps List/Map JSON dispatch stable.
* `typeof<T>()` keeps pointer depth (`typeof<int*>() == "int*"`); both specialize inside generic bodies.
* Method forms: `expr.nameof()` / `expr.typeof()` on any receiver.
* **Enums:** `apple.nameof() == "apple"`; enum *variables* reverse-map the runtime value (`f = cherry; f.nameof() -> "cherry"` via nested ternary).
* Free form: `nameof(id)` (C#-style identifier spelling).
* Simple `__generic_List_String` pretty-print in typeof/nameof for specialization IDs.
* Validated by `cy-validate/val-030-nameof-typeof.cy` (20 tests); full suite 32/32.

**In progress (method generics):** parse + specialise class methods with their own type params
  so `List<U>* Select<U>(U(*fn)(T))` and call sites `xs->Select<String>(fn)` / inferred `U` work.
  Target: replace `SelectString` with one real `Select<U>`; unlock `GroupBy<K>` later on the same machinery.

**Next (after method generics):** UFCS; expand `builtin_method` to dict/seq tables; `?.` `??` `..`; `owned auto` docs.

**Later:** Spread dict, `GroupBy`, properties, list `[]` literals, `is_int<T>` / `Range`.

### Notes from implementation

* `List<T>` private field cross-instance access `other->data[i]` segfaults — use `Count()`/`Get(i)`.
* `for (auto v in other)` inside `List` methods can MIR-bloat with throwing `Get` — use index loops.
* Nested generics: do **not** materialise `List<K>` while `K` is still abstract; placeholders + `specialize_node` + drain.
* Concrete nested (`List<String>` in template body): defer until outer `class_node` is set, then drain `generic_crossrefs`.
* `Range` / all-`T` body typecheck remains.
* Method forward-order: declare callees before callers in the same class.
* Ownership analyzer: prefer `unowned` for intentional alloc/delete loops that reassign the same local each iteration.
