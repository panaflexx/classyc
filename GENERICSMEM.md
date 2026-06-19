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

| Element type `T`                     | `List<T>` | `Set<T>` | Notes |
|--------------------------------------|:---------:|:--------:|-------|
| `int`, `double`, `char`, `bool`, …   | ✅ | ✅ | value stored inline; byte-hash / value-compare |
| `String`                             | ✅ | ✅ | `Set` hashes/compares **by content** (see below) |
| `char*` / other pointers             | ✅ | ✅ | hashed/compared **by address** (identity) |
| `MyClass*` (pointer to a class)      | ✅ | ✅ | **the idiomatic way to put a class in a collection** |
| `MyClass` (class **by value**)       | ❌ | ⚠️ | blocked — see "By-value class elements" |

Rule of thumb: **classes are reference types.** Create them with `new` and store
the pointer: `List<Track*>`, `Set<Track*>`. Use value element types only for
scalars, `String`, and plain pointers.

```c
auto lib  = new List<Track*>();      // ✅ ordered collection of objects
auto favs = new Set<Track*>();       // ✅ identity set of objects
lib->Add(new Track("Kashmir", 508));
```

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

Lowering: checked in `check()` `case N_FORIN` (≈ L13632), generated in `gen()`
`case N_FORIN` (≈ L21950, `class_forin` branch ≈ L22052). Requirements:

* `Count()` must return an integer type.
* `Get(int)` must take a single integer index and **return a scalar or pointer
  type** (≈ L13738). A `Get` returning a class *value* is rejected — the for-in
  codegen stores the element into a MIR register, which only holds scalars/pointers.

### Subscript `coll[i]`

```c
coll[i]        // read  -> coll->Get(i)
coll[i] = v    // write -> coll->Set(i, v)
```

* Read: `check()` `case N_IND` (≈ L13250); `gen()` `case N_IND` (≈ L20165).
* Write: intercepted in `gen()` `case N_ASSIGN` (≈ L19913); falls back to a plain
  store when the class has no `Set(int,T)`.

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
* `get_or_create_specialization()` (≈ L4809) — creates/caches a specialization,
  with guards against instantiating a template whose body failed to parse
  (returns a diagnostic instead of pushing a NULL class and crashing).
* `mangle_generic_name()` — `List` + `MyClass*` → `__generic_List_MyClassP`.

---

## By-value class elements: why `List<MyClass>` / `Set<MyClass>` don't work yet

Storing a `class` **by value** in a generic collection is blocked by three
*independent* gaps. All three must be closed for `List<MyClass>` to work; today
none should be relied upon.

### 1. Codegen: classes can't be passed/returned by value (`undeclared reg`)

A plain function that takes/returns a class **by value** miscompiles, while the
identical `struct` is fine:

```c
class  P { int x, y; };
P  padd(P a, P b)  { P r; r.x=a.x+b.x; r.y=a.y+b.y; return r; }   // ❌ "undeclared reg of func main"

struct P2 { int x, y; };
struct P2 padd2(struct P2 a, struct P2 b) { ... }                 // ✅ works
```

So this is **not** a generics problem — it's the class aggregate calling
convention in `gen`. The proto/arg/return *helpers* already treat `TM_CLASS` as
an aggregate (`get_param_name` ≈ L17236, `simple_return_by_addr_p` ≈ L17247,
`simple_add_arg_proto` ≈ L17323, `simple_add_call_arg_op` ≈ L17336, and
`get_mir_type` returns `MIR_T_UNDEF` for `TM_CLASS`/`TM_STRUCT`/`TM_UNION`
≈ L16392). The defect is that generating the *operand* for a class-typed
argument (or the aggregate return result) yields a bad/garbage register instead
of the expected `MIR_OP_MEM`. Root cause not yet isolated; lives in the `gen()`
`case N_CALL` argument loop (≈ L21054) and the post-call result path
(`target_gen_post_call_res_code`, ≈ L21101).

### 2. `list.h` compares elements with `==`

`IndexOf` / `LastIndexOf` / `Contains` / `Remove` / `Equals` do `data[i] == item`.
Struct/class values cannot be compared with `==` ("invalid types of comparison
operands"), so a by-value class specialization of `list.h` fails to type-check
even if you never call those methods (the whole template is checked). `Set<T>`
avoids this (it uses `SET_EQ`, not `==`), which is why `Set<MyClass>` is closer
to working than `List<MyClass>`.

### 3. `for-in` requires a scalar/pointer `Get`

`Get` returning a class value is rejected by the for-in checker (reason in the
protocol section). `list.h::Concat` uses `for (auto x in other)` internally, so
even ignoring (2), the by-value specialization still won't compile. The for-in
*codegen* is register-based and would need aggregate (stack block-copy) handling
to support struct/class elements — the same gap affects `for (auto s in arr)`
over a C array of structs.

### Also: value-construction syntax

`auto x = List<int>();` / `Point(1,2)` (constructor as a value expression) are
**not parsed** — generic/class instances are constructed with `new`:

```c
auto x = new List<int>();     // ✅
Point* p = new Point(1, 2);   // ✅
```

---

## What to do today

* **Put classes in collections by pointer:** `List<MyClass*>`, `Set<MyClass*>`,
  created with `new`; free the objects yourself (the container frees only its
  own storage). See `examples/classy-collections-class.cy`.
* **Use value element types** for scalars, `String`, and plain pointers:
  `List<int>`, `List<String>`, `Set<String>`, `List<char*>`.
* For content-identity of objects in a `Set`, key on a stable scalar/`String`
  field rather than the object pointer.

## A future "by-value class elements" project

To make `List<MyClass>` / `Set<MyClass>` real, in rough order:

1. **Fix the class aggregate calling convention** so `class P` passes/returns by
   value like `struct P` (the `undeclared reg` bug). This alone unlocks
   `Set<MyClass>` (it needs neither `==` nor for-in).
2. **Add byte-wise `==` / `!=` for class/struct values** (with the usual padding
   caveat) so `list.h`'s search/equality methods specialize.
3. **Allow aggregate element types in `for-in`** (block-copy the element into the
   loop variable's stack slot) — also fixes for-in over arrays of structs.
4. *(optional)* Parse value-construction (`List<int>()`, `Point(1,2)`).

---

## File / symbol index

| Area | Where |
|------|-------|
| Generic template registry / instantiation | `get_or_create_specialization` ≈ L4809 |
| Template deep-copy + pointer-arg fixup     | `specialize_node` ≈ L4646 (N_TYPE fixup ≈ L4731) |
| Type → MIR type (aggregates → `MIR_T_UNDEF`)| `get_mir_type` ≈ L16392 |
| Subscript `coll[i]` read (check / gen)      | `check` N_IND ≈ L13250 / `gen` N_IND ≈ L20165 |
| Subscript `coll[i] = v` write               | `gen` N_ASSIGN ≈ L19913 |
| `for-in` (check / gen)                       | `check` N_FORIN ≈ L13632 / `gen` N_FORIN ≈ L21950 |
| Aggregate calling convention helpers        | ≈ L17236–L17351 |
| `Set<T>` hash/eq dispatch                    | `include/set.h` (`SET_HASH` / `SET_EQ`) |
