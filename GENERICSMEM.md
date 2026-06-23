# Generics, Memory & the Collection Protocol in ClassyC

How `List<T>` and `Set<T>` store their elements, what `T` is allowed to be,
and the duck-typed `Count()` / `Get(int)` / `Set(int,T)` protocol that ties the
language features (`for-in`, `coll[i]`) to user-written collection classes.

This file records findings from getting `List<T>` / `Set<T>` to work with custom
classes (`include/list.h`, `include/set.h`, and the `classy-*` examples). It is
both a user guide ("what works, what to write") and an implementer's map ("where
the relevant code lives, what's still missing").

---

## TL;DR

| Element type `T`                     | `List<T>` | `Set<T>` | `Map<K,V>` | Notes |
|--------------------------------------|:---------:|:--------:|:----------:|-------|
| `int`, `double`, `char`, `bool`, …   | ✅ | ✅ | ✅ | value stored inline; byte-hash / value-compare |
| `String`                             | ✅ | ✅ | ✅ | `Set`/`Map` key hashes/compares **by content** (see below) |
| `char*` / other pointers             | ✅ | ✅ | ✅ | hashed/compared **by address** (identity) |
| `MyClass*` (pointer to a class)      | ✅ | ✅ | ✅ | a class in a collection by reference |
| `MyClass` (class **by value**)       | ✅ | ⚠️ | ⚠️ | stored inline; `List<MyClass>` destroys its elements on `delete` (see "By-value class elements") |

(`Map<K,V>` columns describe both the key type `K` and the value type `V`; `K`
is hashed/compared exactly like a `Set<K>` element.)

Rule of thumb: **classes are reference types.** Create them with `new` and store
the pointer: `List<Track*>`, `Set<Track*>`. Use value element types only for
scalars, `String`, and plain pointers.

```c
auto lib  = new List<Track*>();          // ✅ ordered collection of objects
auto favs = new Set<Track*>();           // ✅ identity set of objects
auto byId = new Map<String, Track*>();   // ✅ keyed collection (string -> object)
lib->Add(new Track("Kashmir", 508));
byId->Set("Kashmir", lib->Get(0));
```

`Map<K, V>` (see `include/map.h`) is the first **two-type-parameter** generic.
It stores keys and values inline (no boxing), keys hashed/compared like a
`Set<K>` (String by content, scalars by value, objects by identity), and plugs
into the same language sugar as `List`/`Set` plus a *keyed* variant of the
subscript and for-in protocols (see
[Multiple type parameters](#multiple-type-parameters-mapk-v) below).

---

## Two storage modes

A generic collection `class List<T> { T* data; ... }` lays its backing array out
as `sizeof(T) * capacity` bytes. What `T` *is* therefore decides everything:

* **Value/scalar `T`** (`int`, `double`, `String`, `char*`, `MyClass*`):
  `T` is a register-sized value (≤ 8 bytes). `data[i]` is a normal element slot,
  `Get` returns the value, `Add` copies it. Works everywhere.

* **By-reference `T = MyClass*`**: the element *is* a pointer. The pointed-to
  object lives on the heap (`new`). The collection owns the pointers, not the
  objects (you free the objects yourself). This is the supported way to keep
  class instances in a collection.

* **By-value `T = MyClass`**: the element would be the whole class struct stored
  inline. **Not currently supported** (three independent reasons below).

---

## The collection protocol: `Count()` / `Get(int)` / `Set(int,T)`

Several language features are *duck-typed* over any class exposing these methods —
this is why the standard `List<T>` and a user's own collection behave identically.

### `for-in`

```c
for (auto x in coll)      // x = coll->Get(i)  for i in [0, coll->Count())
for (auto i, x in coll)   // i = index (int),  x = element
```

Lowering: checked in `check()` `case N_FORIN`, generated in `gen()`
`case N_FORIN` (`class_forin` branch). Requirements:

* `Count()` must return an integer type.
* `Get(int)` must take a single integer index and **return a scalar or pointer
  type**. A `Get` returning a class *value* is rejected — the for-in
  codegen stores the element into a MIR register, which only holds scalars/pointers.

#### Keyed for-in (the `Map<K,V>` variant)

A class exposing `Count()` **plus** `KeyAt(int)` and `ValAt(int)` is treated as a
*keyed* collection, and for-in binds **(key, value)** rather than (index, element):

```c
for (auto k in map)       // k = map->KeyAt(i)            (keys, in insertion order)
for (auto k, v in map)    // k = map->KeyAt(i), v = map->ValAt(i)
```

This mirrors the built-in `dict`'s `for (auto k, val in d)`. The keyed protocol
is detected first in both `check()` and `gen()`; the `KeyAt`/`ValAt` return
types (which must be scalar/pointer) become the loop-variable types. Classes
without `KeyAt`/`ValAt` (e.g. `List`/`Set`) fall back to the index `Get`
protocol unchanged, so there is no behavioural change for existing collections.

### Subscript `coll[i]`

```c
coll[i]        // read  -> coll->Get(i)
coll[i] = v    // write -> coll->Set(i, v)
```

* Read: `check()` `case N_IND`; `gen()` `case N_IND`.
* Write: intercepted in `gen()` `case N_ASSIGN`; falls back to a plain
  store when the class has no `Set(int,T)`.

**Keyed subscript (non-integer keys).** The subscript protocol is no longer
restricted to integer indices. `check()` `case N_IND` now reads `Get`'s key
*parameter* type: if it is integer, an integer index is required (List/Set,
unchanged); otherwise any index assignable to the key type is accepted, so
`Map<String,V>` supports `m["name"]` (read → `Get(String)`) and
`m["name"] = v` (write → `Set(String, V)`). On the gen side the index is only
widened to `I64` when it is integer; a non-integer key is passed through and
coerced to the key parameter type by `gen_funcptr_call`. (Floating-point and
aggregate keys via subscript are not meaningful here — use `Get`/`Set` directly.)

**Subscript vs. raw pointer indexing (fixed).** `p[i]` where `p` is a
*pointer to a class* used to *always* try the `Get` sugar and error with
`class type has no Get(int) method for [] subscript` when the class had no
`Get`. That broke the `T* data; data[i]` backing array of any collection
specialised over a class type. It now falls back to ordinary C pointer indexing
when the class has no `Get` method (check + gen, ≈ L13258 / L20165), so both
`MyClass* p; p[i]` and by-value class backing arrays index correctly.

---

## `Set<T>` hashing & equality

`include/set.h` cannot rely on `==` for element equality, because **ClassyC's
`==` on `String` is pointer identity, not content** (and string literals are not
interned — two `"alice"` literals compare unequal). The set picks the right
hash/equality pair at compile time with C11 `_Generic`:

```c
#define SET_HASH(k)   (_Generic((k), String: set_hash_strkey, default: set_hash_bytes)(&(k), sizeof(k)))
#define SET_EQ(a, b)  (_Generic((a), String: set_eq_strkey,   default: set_eq_bytes)(&(a), &(b), sizeof(a)))
```

Both arms of each `_Generic` share one signature, so the single call site
type-checks for *every* specialization `T`. Result:

* `Set<String>`  → **content** hashing/equality (FNV-1a over the bytes / `strcmp`).
* `Set<int>`, `Set<double>`, small PODs → **byte-wise** hashing/equality.
* `Set<MyClass*>` → hashes the **pointer bits**, i.e. **identity** semantics —
  exactly right for "the set of objects I've favourited / visited / selected".

Consequence: `Set<MyClass*>` deduplicates and intersects by object identity, not
by field contents. If you need content-based identity for objects, give each
object a stable key (e.g. an `int id` or a `String`) and key a `Set` on that.

---

## Generic specialization and pointer type arguments

A generic class is parsed once into a template, then deep-copied per type
argument by `specialize_node()` (≈ L4646), substituting the type-parameter
N_IDs. Pointer type arguments (`List<char*>`, `List<MyClass*>`) need care:

* The N_ID substitution strips pointer levels off the argument and inserts only
  the **base** type into the type-specifier list (≈ L4656).
* A fixup pass then re-injects the pointer level(s) into the *declarator's*
  decoration list (≈ L4721). It must cover `N_MEMBER`, `N_SPEC_DECL`,
  `N_FUNC_DEF`, **and `N_TYPE`**.

**The `N_TYPE` case (fixed).** Abstract declarators are `N_TYPE` nodes:
the unnamed parameters of a function-pointer type (`int(*cmp)(T,T)`), `(T*)`
casts, and `sizeof(T)`. Without the `N_TYPE` fixup, `T = MyClass*` collapsed to a
by-value `MyClass` there, so `List<MyClass*>::Sort/Filter/ForEach` failed with
`cannot pass a 'MyClass *' where a by-value 'MyClass' parameter is expected`, and
`(T*)`/`sizeof(T)` produced "incompatible types"/wrong sizes. Adding `N_TYPE` to
the fixup makes `List<MyClass*>` work end-to-end with the higher-order methods.

Other relevant spots:
* `get_or_create_specialization()` — creates/caches a specialization,
  with guards against instantiating a template whose body failed to parse
  (returns a diagnostic instead of pushing a NULL class and crashing).
* `mangle_generic_name()` — `List` + `MyClass*` → `__generic_List_MyClassP`.

---

## Multiple type parameters: `Map<K, V>`

`Map<K, V>` (`include/map.h`) is the first generic with **more than one** type
parameter. Most of the multi-parameter machinery was already in place — it just
hadn't been exercised:

* The parser stores up to **4** type parameters per template
  (`generic_tmpl_t::type_params[4]`) and parses both the declaration
  (`class Map<K, V> { ... }`) and the instantiation (`Map<String, int>`) as
  comma-separated lists.
* `mangle_generic_name()` already loops over every argument, so
  `Map` + `String` + `int` → `__generic_Map_String_int`, and
  `Map<String, Track*>` → `__generic_Map_String_TrackP`.
* `specialize_node()`'s type-parameter substitution and the pointer-arg
  declarator fixup (`N_MEMBER` / `N_SPEC_DECL` / `N_FUNC_DEF` / `N_TYPE`) both
  iterate `n_params`, so `K*`/`V*` fields, `sizeof(K)`/`sizeof(V)`, `(K*)` casts,
  and `int(*)(K,K)` callbacks specialise correctly for two parameters.

### The one gap that needed fixing: multi-param self-reference

Inside a generic body, a reference to the class's **own** type
(`Map<K, V>* Copy()`, `new Map<K, V>()`) is recorded in the template AST as a
mangled *placeholder* N_ID built from the parameter names — `__generic_Map_K_V`.
When the template is specialised, `specialize_node()` must rewrite that
placeholder to the concrete name (`__generic_Map_String_int`).

The old code only handled the **single**-parameter case: it compared the part
after `__generic_<Orig>_` against one parameter name (`T` → `__generic_List_T`).
For `Map<K,V>` the rest was `K_V`, which matched neither `K` nor `V`, so the
placeholder leaked through unresolved and produced
`unknown type __generic_Map_K_V`.

The fix generalises the placeholder resolver: it tokenises the suffix on `_`,
maps each token back to a parameter index, collects the corresponding concrete
arguments, and re-mangles with all of them. It works for any parameter count
and reduces to the original behaviour for one parameter. (It assumes parameter
names contain no `_`, which holds for `T`, `K`, `V`, `Key`, `Value`, ….) This
is the *only* compiler change required to let a two-parameter generic refer to
itself, so `Copy()`/`Merge()`/internal `new Map<K,V>()` all work.

### Still unsupported: cross-generic references with an *unresolved* parameter

A generic body that instantiates a **different** generic with one of its own
(still-abstract) parameters — e.g. `List<K>` inside `Map<K, V>` (think a
hypothetical `Keys() -> List<K>*`) — does **not** work: `List<K>` tries to
materialise `__generic_List_K` immediately, with `K` an unresolved identifier,
yielding `unknown type K`. `map.h` therefore avoids it: instead of returning a
`List<K>`/`List<V>`, the map exposes its own `Count()` / `KeyAt(int)` /
`ValAt(int)` traversal (which also powers keyed for-in), plus `ForEach`.
Closing this gap (deferred specialisation of nested generics) would let keyed
collections hand back `List<K>` of their keys directly.

### `Map<K, V>` hashing

`map.h` reuses the `set.h` strategy verbatim, on the **key** type `K`:
`_Generic` selects content hashing/equality for `String` keys and byte-wise
hashing for everything else (scalars by value, pointers/objects by identity).
Keys and values live in **parallel dense arrays** (`K* keys; V* vals;`) indexed
by an open-addressing table of dense indices — the same layout as `Set<T>`, with
a second value array. Insertion order is preserved, which is what `KeyAt`/
`ValAt` and keyed for-in iterate.

---

## By-value class elements: `List<MyClass>` now works

Storing a `class` **by value** in a `List<T>` is supported: elements live inline
in the backing buffer (cache-friendly, no per-element heap allocation), and the
collection **owns** them — `delete list` (or a `defer delete`) runs each live
element's destructor before freeing the buffer. Four pieces had to come
together; each is now in place.

### 1. ABI: classes pass / return by value like structs

A function that takes/returns a class by value now compiles and runs:

```c
class P { int x, y; };
P padd(P a, P b) { P r; r.x=a.x+b.x; r.y=a.y+b.y; return r; }   // ✅ works
```

The fix was in `gen()` `case N_CALL`: the result paths that special-cased
`TM_STRUCT || TM_UNION` (the by-reg aggregate result, the va_arg block, and the
check-side call-arg-area sizing) now include `TM_CLASS`, so a class result yields
the expected `MIR_OP_MEM`/block-move instead of a garbage register. The ABI
*helpers* (`simple_return_by_addr_p`, `simple_add_arg_proto`,
`simple_add_call_arg_op`, `target_*` in `cx86_64-ABI-code.c`) already handled
`TM_CLASS`. See `examples/test-byval-abi.cy`.

### 2. `==` / `!=` on class/struct values (byte-wise)

`IndexOf` / `LastIndexOf` / `Contains` / `Remove` / `Equals` do `data[i] == item`.
For a by-value class/struct, `a == b` / `a != b` is now lowered to
`memcmp(&a, &b, sizeof) (== / != 0)` (shallow, byte-wise equality). Checked in
`check` N_EQ/N_NE; emitted in `gen` via `gen_memcmp`. Caveat: padding bytes
participate, so it is only well-defined for fully-initialized values.

### 3. `for-in` over aggregate elements

`Get` returning a class value is accepted by the for-in checker, and the for-in
*codegen* now block-copies the aggregate element into the loop variable's stack
slot (the loop var is registered for frame allocation; small aggregates returned
in registers are scattered into the slot, larger ones constructed directly into
it via the hidden-pointer return). This makes `list.h::Concat`'s
`for (auto x in other)` specialize for a by-value `T`.

### 4. Element destruction: `__destroy(x)` intrinsic

So that the collection can destroy what it owns, `~List()` calls a compiler
intrinsic in a loop:

```c
~List() {
    for (int i = 0; i < this->length; i++) __destroy(this->data[i]);
    if (this->data) free((void*) this->data);
}
```

`__destroy(x)` expands to `x`'s destructor call when `x` is a by-value class with
a user `~T()`, and to **nothing** for scalars, `String`, and pointer element
types — so `List<int>` / `List<String>` / `List<char*>` are unchanged. Keeping
the loop in the template (which knows its `data`/`length` fields) avoids
hard-coding collection internals into the compiler. See
`examples/test-list-byval.cy`.

### Value-construction syntax

Stack construction is parsed and lowered to in-place construction (no temporary,
no by-value return) reusing the existing RAII ctor/dtor machinery:

```c
Point p = Point(1, 2);        // ✅ ctor runs in place; ~Point() at scope exit
Point* h = new Point(1, 2);   // ✅ heap (unchanged)
```

`auto x = List<int>();` (a generic instance as a *value* expression) is still
constructed with `new`.

---

## Still open

* **`Set<MyClass>` / `Map<K, MyClass>` by value:** the ABI, `==`, and for-in
  fixes apply, but `set.h` / `map.h` were not updated to call `__destroy` on
  their elements, so a by-value class `Set`/`Map` would leak element-owned
  resources on delete. Add the same `__destroy` loop to their destructors to
  finish this.
* **`==` padding caveat:** consider a member-wise compare (or requiring an
  `equals` protocol method) for classes with padding or pointer members where
  byte equality is not the intended semantics.

---

## File / symbol index

| Area | Where |
|------|-------|
| Generic template registry / instantiation | `get_or_create_specialization` |
| Template deep-copy + pointer-arg fixup     | `specialize_node` (N_TYPE fixup) |
| Multi-param self-reference placeholder      ≈ L4646 (N_TYPE fixup ≈ L4731) |
| Type → MIR type (aggregates → `MIR_T_UNDEF`)| `get_mir_type` ≈ L16392 |
| Subscript `coll[i]` read (check / gen)      | `check` N_IND ≈ L13250 / `gen` N_IND ≈ L20165 |
| Subscript `coll[i] = v` write               | `gen` N_ASSIGN ≈ L19913 |
| `for-in` (check / gen)                       | `check` N_FORIN ≈ L13632 / `gen` N_FORIN ≈ L21950 |
| Aggregate calling convention helpers        | ≈ L17236–L17351 |
| `Set<T>` hash/eq dispatch                    | `include/set.h` (`SET_HASH` / `SET_EQ`) |
