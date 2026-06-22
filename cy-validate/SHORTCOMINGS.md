# ClassyC — Shortcomings & Gotchas (validation findings)

Things that **don't work as the README / intuition suggests**, discovered while
building the `cy-validate/` suite, each with a **workaround**. Living document —
updated as validation proceeds.

Compiler/runtime tested via: `./bin/classyc -g -I include <file>.cy -eg`

---

## A. README inaccuracies (code that does NOT compile/run as written)

### A1. `String.checkpoint()` / `String.release_to()` are not real APIs
README "Memory Management" and the `defer` example show:
```c
String arena = String.checkpoint();
defer String.release_to(arena);
```
**Result:** compile error — `no static method 'checkpoint' on String`,
`unknown String static method 'checkpoint'` (same for `release_to`).

**Reality:** String arena reclamation is *fully automatic*. The compiler emits
`c2m_str_checkpoint()` / `c2m_str_release_to()` / `c2m_str_release_keeping()`
around allocating scopes by itself (see `src/classyc.c`, `subtree_allocates_string_p`
and the `gen` N_BLOCK / N_RETURN paths). There is no user-facing surface.

**Workaround:** delete these calls; rely on automatic reclamation (validated in
`val-002-string-arena.cy`). For a leak-free hot loop just allocate normally.

### A2. `String.replace(needle, replacement)` does not exist — `replace` is positional
README String example:
```c
if (path.find(".pdf")) path = path.replace(".pdf", ".txt");
```
**Result:** `String method 'replace' expects 3 arguments`.

**Reality:** `replace(pos, len, repl)` — code-point position, length, replacement
(`SM_REPLACE`, n=3). It is *not* a search-and-replace.

**Workaround:**
```c
size_t i = path.find(".pdf");
if (i != (size_t)-1) path = path.replace(i, 4, ".txt");
```

### A3. `if (s.find(x))` is a buggy idiom
`find` returns a **code-point index**, or **`(size_t)-1` when not found**.
So `(size_t)-1` is *truthy* (looks "found") and a match at index 0 is *falsy*.

**Workaround:** always compare explicitly: `if (s.find(x) != (size_t)-1)`.

### A4. `printf("%s", dict_value)` is rejected / unsafe
README's flagship dict example:
```c
printf("%s\n", cfg.server.host);   // compile error
printf("%s\n", cfg["timeout"]);    // compile error; also timeout is numeric
```
**Result:** `format '%s' expects a string or pointer argument but the
corresponding argument has non-pointer type`.

**Reality:** dict dot/subscript yields a `dict` value the static printf checker
treats as non-pointer. Even after `(char*)`, `cfg["timeout"]` is a *number*, so
`%s` dereferences a non-string and **segfaults**.

**Workaround:** stringify any value with the `json()` builtin, or cast a known
*string* leaf: `printf("%s\n", (char*)cfg.server.host);` /
`printf("%s\n", (char*)json(v));`. Read numeric leaves as scalars (see A8).

### A5. `-fno-exceptions` is NOT the default
README: *"Enabled with `-fexceptions` (default off; disable with
`-fno-exceptions`)"*.

**Reality:** `options.exceptions_p = TRUE` in `src/classyc-driver.c` —
exceptions **and** the JIT safety guards (null-deref / div-by-zero / OOB) are
**ON by default**. `-fno-exceptions` turns them off.

**Workaround:** none needed; just know guards are on unless you pass
`-fno-exceptions`.

---

## B. Language ergonomics / sharp edges

### B1. String methods need a `String`-typed receiver
`"abc".equals("abc")` and `"MiXeD".lower()` fail:
`request for member ... in something not a structure, union, class or dict`.

**Workaround:** bind to a `String` var first, or cast: `((String)"abc").lower()`.

### B2. `+` concat needs a `String`-typed left operand
`"tmp" + i` is C pointer arithmetic (char* + int), not concatenation.

**Workaround:** make the left side a String: `(String)"tmp" + i`, or start from
a `String` variable.

### B3. `#include "list.h"` / `"map.h"` / `"set.h"` require `-I include`
README shows bare `#include "map.h"`. With the default `examples/run-examples.sh`
invocation (no `-I`) you must write `#include "include/list.h"`.

**Workaround:** run with `-I include` (this suite does) **or** use the
`include/`-prefixed path.

### B4. README generics example is wrong on multiple counts
README:
```c
List<int> nums = {1, 2, 3};
nums = nums.Filter((int x) => x > 1).Map((int x) => x * 2);
```
Problems:
* `List<int> nums = {1,2,3}` (value, non-pointer) warns
  `assigning integer without cast to pointer` — `List<T>` is a reference type.
* `.Map(...)` does **not exist** on `List<T>`: `class has no member Map`.
  `include/list.h` provides `Filter` and `ForEach` but no `Map`.
* Real examples use pointer + arrow: `new List<int>{...}` and `xs->Filter(...)`.

**Workaround:** use the heap idiom and the methods that exist:
```c
List<int>* nums = new List<int>{ 1, 2, 3 };
List<int>* big2 = nums->Filter((int x) => x > 1);   // no Map; transform manually
defer delete nums; defer delete big2;
```
For map/filter/reduce pipelines over a **C array/slice**, use the lowercase seq
methods instead: `arr.filter(..).map(..).ToList()`.

### B5. Stack class instance with constructor arguments is not supported
```c
Wizard w = Wizard(5, 1, 'L');   // error: called object is not a function...
```
Classes are reference types; there is no value-construction-with-args form.
A default-constructed stack object (e.g. `StringPet p; p.name = ...;`) works
only when the class has a zero-arg construction and you set fields manually.

**Workaround:** allocate on the heap — `Wizard* w = new Wizard(5,1,'L'); defer
delete w;` — which is the idiomatic form for objects with constructors.

---

## C. `dict` array support (partial)

### C1. Array *literal* assignment not implemented
`d.tags = ["fast", "safe"];` — not supported (confirmed by the commented-out
sections in `examples/classy-dict-arena.cy`).

**Workaround:** create arrays through JSON: `dict d = json("{\"t\":[1,2,3]}");`
(round-trips and serializes correctly), or the runtime
`dict_create_array` / `dict_array_append` helpers.

### C2. Deep numeric leaf access unwraps to a raw scalar (crash if treated as dict)
For `dict d = json("{\"items\":[{\"value\":7}]}");`:
```c
dict v = d.items[0].value;   // v becomes the raw int 7 reinterpreted as a dict
json(v);                     // dereferences address 0x7 -> SIGSEGV
```
String leaves (`d.items[0].name`) behave like `char*` and are fine.

**Workaround:** read numeric leaves directly as scalars:
`int x = (int)d.items[0].value;` (validated to give 7/9). Reserve `json()` for
dict/array/string values.

### C3. `for-in` does not iterate a dict array value
`for (auto x in d.items)` runs **0 iterations** even when the JSON array is
non-empty.

**Workaround:** index by integer with a known count: `d.items[i]`. There is no
exposed array-length builtin for dict arrays.

---

## E. Interfaces & `Any<I>` rough edges

### E0. (FIXED) Returning an `Any<I>` handle from a function was a use-after-free
`return any<I>(new C(...));` (or `return handle;`) used to hand the caller a
freed pointer: the object arena released the handle at the function's scope
exit, so a later `h->method()` read freed/zeroed memory (often `area = 0.0000`,
sometimes a SIGSEGV inside `__thunk_<m>_<Class>`).

**Root cause:** the N_RETURN codegen released the object arena
(`gen_obj_release_to`) without protecting the returned handle — unlike Strings,
which are protected by `gen_str_release_keeping`.

**Fix (applied to `src/classyc.c`, N_RETURN):** when returning a pointer while
the object scope is active, detach the returned value from the arena
(`gen_obj_detach`) before releasing, transferring ownership to the caller:
```c
if (obj_scope_active) {
  if (!ret_by_addr_p && scalar_p && ret_type->mode == TM_PTR)
    gen_obj_detach (c2m_ctx, val.mir_op);
  gen_obj_release_to (c2m_ctx, obj_scope_mark.mir_op);
}
```
Validated by `val-013-any-edge.cy` ("return Any<Shape>* from a function") and
`val-014-any-return-mem.cy`. The caller now owns the returned handle and is
responsible for `delete`-ing it (consistent with the `new` ownership model).
NOTE: returning a *collection* of handles still frees the contained handles
(see the remaining limitation in E1's neighborhood).

### E1. (BUG) Erasing the SAME class to two different interfaces fails codegen
Minimal repro:
```c
interface Shape     { double area(); }
interface Printable { String describe(); }
class Square { ... double area(); String describe(); ~Square(){} };

Any<Shape>*     a = any<Shape>(new Square(2.0));
Any<Printable>* b = any<Printable>(new Square(3.0));   // <-- here
```
**Result:** `Repeated item declaration __thunk_dtor_Square` (MIR aborts).
The destructor thunk is emitted per *(class × interface)* instead of once per
class, so the second erasure redeclares `__thunk_dtor_Square`.

**Workaround:** erase a given concrete class through **one** interface only. If
you need several capabilities, declare a single interface that lists all the
methods and erase to that; or wrap distinct concrete classes per interface.

### E2. (works) Conformance checking is sound — both opt-in and structural
- `class C impl I` missing a method:
  `class C does not satisfy interface I: missing <m>()` (compile error). ✅
- `any<I>(new C(...))` where `C` lacks a method:
  `any<I>: class C does not satisfy interface I: missing <m>()`. ✅
- Calling a method that exists on the class but is **not** in the interface,
  through an erased handle: `class has no member <m>` — the handle exposes only
  the interface surface. ✅

### E3. (works) Non-trivial erased dispatch is fine
Validated to work through `Any<I>*`: non-`void` returns (`double`, `String`),
methods that take arguments (`scale(double)`), mutation through the handle,
passing a handle to a function, returning a handle from a function, and storing
handles in `List<Any<I>*>` **and** `Map<K, Any<I>*>` (for-in over both). See
`val-012-interfaces-any.cy` and `val-013-any-edge.cy`.

---

## D. What works well (for contrast)
- `dict` objects: init, nested init, dot read/write, dynamic key creation,
  `[ ]` subscript, `"k" in d`, `for (auto k in d)`, `for (auto k, v in d)`,
  `json()` parse/serialize round-trip, `d.json` shorthand.
- dict JSON arrays at the value level: `d.nums` retrieves it, `json(d.nums)`
  -> `[10,20,30]`, integer subscript `d.nums[0]` returns the element.
- Automatic String arena: returns survive (release_keeping), tight 200k-alloc
  loops stay bounded (~18 MB RSS) and correct.
- The runtime stack-trace on faults (shows `main() [file:line]`).
