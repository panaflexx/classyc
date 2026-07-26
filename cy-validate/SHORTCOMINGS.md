# ClassyC — Shortcomings & Gotchas (validation findings)

Actual **language/compiler** shortcomings and gotchas, discovered while building
the `cy-validate/` suite, each with a **workaround**. Living document.

Compiler/runtime tested via: `./bin/classyc -g -I include <file>.cy -eg`

> **Status:** the top-level `README.md` has been **updated** to match these
> findings — every code snippet in it now compiles and runs as written
> (verified end-to-end). The items in section A below were the README
> mismatches that prompted those edits; they are kept here because they describe
> the *real language behavior* you still need to know (the README now documents
> it correctly rather than misleadingly). Sections B–E are genuine language /
> compiler shortcomings. Several have since been **fixed**: E0 & E1 (the
> `Any<I>` return-handle UAF and the dual-interface thunk duplication), plus
> three ergonomics wins — A2 (`replace(needle, repl)` search-and-replace), B1
> (String methods on a string-literal receiver), and B4 (`List<T>.Map`). Each
> fix is marked inline and validated in `val-015` (and `val-013` for E1).
>
> **NEW (F0 + F1, fixed):** the automatic String / object arena now handles
> cross-function allocations (`String` returned from any helper, including
> `json()` and `List<String>.join(...)`) and is layered with a **per-iteration
> loop scope** so tight loops driven by helper calls stay bounded — without
> the user calling `c2m_str_checkpoint`/`release_to` manually (see
> `examples/classy-fetch.cy` for the original manual pattern this replaces).
> Validated in `val-019-loop-arena.cy`.

---

## F. Automatic arena reclamation across helpers & loop iterations

### F0. (FIXED) Cross-function `String` allocations now activate the caller's arena
**Before:** `subtree_allocates_string_p` in `src/classyc.c` only looked for
`N_CONCAT` and a handful of method-specific `N_CALL` patterns rooted at
`String`/string-literal receivers. A function like

```c
String label(int i) { return (String)"x#" + i; }
int main(void) {
    for (int i = 0; i < 200000; i++) { String t = label(i); /* ... */ }
}
```

left `main`'s body looking allocation-free to the detector (no inline
`N_CONCAT`, no string-literal method receiver), so no function-level
checkpoint was emitted and 200 k tracked Strings piled up until `main`
returned. Same issue for `json(v)`, `list->join(",")`, or any user helper
that builds a `String` internally.

**Fix:** the detector now treats **any `N_CALL` whose result type is
`String`** as allocating in the caller's scope (return values are protected
by `release_keeping` at the callee's `N_RETURN`, so they survive into the
caller and *do* need cleanup there). The older method-specific patterns are
kept as a defensive fast-path. Defensive `SM_JOIN` on `List<String>`
receivers added in case the call expr's type isn't yet populated. See
`subtree_allocates_string_p` (`src/classyc.c`).

### F1. (FIXED) Per-iteration loop arena: bounded memory without manual hooks
**Before:** the function-level checkpoint only released at function exit, so
a hot loop allocating many Strings per iteration kept them ALL alive until
the enclosing function returned. The work-around in `examples/classy-fetch.cy`
was a manual `c2m_str_checkpoint()` / `c2m_str_release_to(mark)` pair at the
start and end of each iteration body.

**Fix:** `N_FOR`, `N_WHILE`, `N_DO`, and `N_FORIN` now emit a per-iteration
String checkpoint at the top of the body and a release at the back-edge
(`continue_label` for for/do/for-in, just before the back-edge cond for
while). `N_CONTINUE` and `N_BREAK` emit a release before they jump, with
`break` skipping the release when it exits a `switch` nested in the loop
(`break_label != loop_break_label_for_scope`). The same machinery is
layered for the object arena (`Any<I>` handles). State lives in `gen_ctx`
fields `loop_str_scope_*` / `loop_obj_scope_*` / `loop_break_label_for_scope`;
helpers are `gen_loop_body_scope_enter/release/leave` in `src/classyc.c`.

**Per-iter is layered, not replacing, the function-level scope:**
N_RETURN still releases back to the function-level mark and protects the
returned String with `release_keeping`. So a String *returned* from a
function whose loop took per-iter checkpoints still survives the per-iter
release — verified by `val-019` test (c).

**Safety guard against "escape via assignment":** if a loop body assigns
through a tracked-typed identifier (`outerString = helper(i);`), the
assigned value may outlive the iteration and a per-iter release would
dangle the variable. The compiler detects this conservatively with
`subtree_assigns_tracked_id_p` and **disables** per-iter for that loop
(falls back to function-level cleanup at return). The common bounded
patterns — `String t = helper(i);` (init, not assign), `Http.get(...)`
returning a class pointer with `defer delete`, dict / header lookups
consumed inline — remain eligible.

**Known limit:** escape via a method/function call (e.g.
`outerList->Add(s)` storing a tracked String into an outer collection) is
NOT detected. Such patterns will still dangle. Users in doubt can either
store the result through an assignment (suppressing per-iter) or wrap the
inner work in a function (so the helper's function-level scope owns the
lifetime).

Validated by `cy-validate/val-019-loop-arena.cy`:
- 200 k-iter helper-only loop grows ~28 kB RSS (~0.14 B/iter — slack only);
- nested 1000×1000 `break` loop bounded under 8 MB;
- a String returned from inside a per-iter loop scope survives the release.

---

## A. Language behaviors the README used to get wrong (README now fixed)

These are real, intentional language behaviors. The README previously showed
code that didn't compile/run; it has been corrected. Kept here as gotchas.

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

### A2. (FIXED) `replace` is now overloaded — `replace(needle, repl)` is search-and-replace
The README example now works as written:
```c
if (path.contains(".pdf")) path = path.replace(".pdf", ".txt");
```
**Reality:** `replace` is overloaded by arity:
- `replace(pos, len, repl)` (3 args) — the original positional form (code-point
  position, length, replacement; `SM_REPLACE`).
- `replace(needle, repl)` (2 args) — search-and-replace of **every** occurrence
  (`SM_REPLACE_ALL`, backed by the new `c2m_str_replace_all` runtime). A longer
  or shorter replacement, an empty replacement (deletion), and an absent needle
  all behave as expected. Validated in `val-015-string-literal-and-replace.cy`.

**Note:** prefer `contains()` over the old `find()` truthiness idiom (see A3).

### A3. `if (s.find(x))` is a buggy idiom — use `contains()`
`find` returns a **code-point index**, or **`(size_t)-1` when not found**.
So `(size_t)-1` is *truthy* (looks "found") and a match at index 0 is *falsy*.

**Workaround:** there is a dedicated boolean: `if (s.contains(x))`. If you need
the index, compare explicitly: `if (s.find(x) != (size_t)-1)`.

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

### B1. (FIXED) String methods now work on a string-literal receiver
`"abc".equals("abc")` and `"MiXeD".lower()` used to fail with
`request for member ... in something not a structure, union, class or dict`.

**Reality:** a UTF-8 string literal (`N_STR` node) used as a method receiver is
now dispatched as a `String` instance method, the same as a `String`-typed
value — no `((String)..)` cast needed. The bare `String` type keyword
(`N_STRING` node) is still the *static* receiver for `String.copy/attach`.
Allocating results (`upper`/`lower`/`substr`/`trim`/`replace`) are arena-tracked,
so tight loops stay bounded. Validated in `val-015-string-literal-and-replace.cy`.

**No workaround needed.**

### B2. `+` concat needs a `String`-typed left operand
`"tmp" + i` is C pointer arithmetic (char* + int), not concatenation.

**Workaround:** make the left side a String: `(String)"tmp" + i`, or start from
a `String` variable.

### B3. `#include "list.h"` / `"map.h"` / `"set.h"` require `-I include`
README shows bare `#include "map.h"`. With the default `examples/run-examples.sh`
invocation (no `-I`) you must write `#include "include/list.h"`.

**Workaround:** run with `-I include` (this suite does) **or** use the
`include/`-prefixed path.

### B4. (PARTIALLY FIXED) `List<T>.Map` now exists; the value-init caveat remains
README:
```c
List<int> nums = {1, 2, 3};
nums = nums.Filter((int x) => x > 1).Map((int x) => x * 2);
```
* `List<int> nums = {1,2,3}` (value, non-pointer): **FIXED** — stack/value
  collections now exist (`auto xs = List<int>();`, `b = move a`, brace-init
  `new List<Pt>{ Pt(1,2) }`; validated in `val-038`/`val-039`/`val-040`).
  The heap idiom `new List<int>{...}` and `->` still works too.
* `Map` now **exists** on `List<T>` (`include/list.h`), as a same-type transform
  `List<T>* Map(T(*fn)(T))` that chains with `Filter`/`ForEach`. (A cross-type
  `T -> U` map would need a second type parameter; for that, use the lowercase
  seq methods over a C array/slice: `arr.filter(..).map(..).ToList()`.)

**Idiomatic form (validated in `val-015`):**
```c
List<int>* nums = new List<int>{ 1, 2, 3 };
List<int>* big2 = nums->Filter((int x) => x > 1)->Map((int x) => x * 2);
defer delete nums; defer delete big2;
```

### B5. (FIXED) Stack class instance with constructor arguments
```c
Wizard w = Wizard(5, 1, 'L');   // now works: in-place ctor + ~Wizard() at scope exit
```
Stack value-construction with constructor arguments now works for plain
classes: `Point p = Point(1, 2);` runs the constructor in place and the
destructor at scope exit (RAII, no `new`/`delete` needed).  The **generic
collections** (`List<T>` / `Set<T>` / `Map<K,V>`) also have a stack/value
form now (`auto xs = List<Pt>();`, move-only transfer; `val-038`–`val-040`)
in addition to the heap `new` form.

---

## C. `dict` array support (partial)

### C1. Array *literal* assignment not implemented
`d.tags = ["fast", "safe"];` — not supported (confirmed by the commented-out
sections in `examples/classy-dict-arena.cy`).

**Workaround:** create arrays through JSON: `dict d = json("{\"t\":[1,2,3]}");`
(round-trips and serializes correctly), or the runtime
`dict_create_array` / `dict_array_append` helpers.

### C1b. (FIXED) `d.length()` / `d.count()` expose the size
For any dict, `d.length()` and `d.count()` return the unified iteration size:
array length for `DICT_ARRAY`, pair count for `DICT_OBJECT`, `0` otherwise.
They are methods (not magic properties) so they cannot collide with real dict
keys named `length` / `count` — those still round-trip through the normal
`d.length = ...` / `d.length` runtime lookup.  Implementation:
`dict_iter_count` in `ext/mir/inc/dict.h` plus the N_CALL dispatch in
`src/classyc.c`.

### C2. (FIXED) Deep dict-leaf access stays a tagged DictValue *
`d.items[0].value` (and every other leaf field access on a dict) now produces
a tagged `DictValue*` instead of an unwrapped scalar.  The previous behaviour
special-cased the field names `.value` (→ `int`) and `.desc` (→ `char*`),
which made
```c
dict v = d.items[0].value;
json(v);  // SIGSEGV: 7 reinterpreted as DictValue*
```
dereference `0x7`.  Today `json(v)` returns `"7"`.

To read a numeric/string leaf as a plain C scalar, use an explicit cast:
`int x = (int)d.items[0].value;`, `char *s = (char*)d.items[0].name;`.
The cast triggers the existing dict-union unwrap path; the box is what a
future typed JSON binder (`dict → class` field assignment) walks.

### C3. (FIXED) `for-in` over a dict array iterates correctly
Dict for-in now dispatches at runtime on the `DictType` tag:
* `DICT_OBJECT` — single-var binds the key (`char*`); two-var binds
  `(key, value)`.
* `DICT_ARRAY` — single-var counts the elements (loop variable is still typed
  `char*` per the existing dict for-in convention, holding a `DictValue*` at
  MIR level); two-var binds `(index, element)` with the element typed as
  `dict` so chained access like `x.name` and `(int)x` both work.

Runtime helpers added: `dict_is_array`, `dict_iter_count` in `dict.h`; gen
side in `src/classyc.c` (FORIN dict branch).  Covered by
`cy-validate/val-004-dict-arrays.cy`.

### C4. (LANDED) Typed JSON binding — Phase 2 (collection fields)
`(T) d` / `(T)? d` now populates `List<T>*` (and any `Add(T)`-protocol
collection, e.g. `Set<T>*`) from a JSON array field.  The binder allocates the
collection, calls its default ctor, loops the dict array unwrapping each
element (scalar / String private-copy / nested object via recursion into
`gen_dict_bind_into`), and calls `Add`.  The bound object owns the heap
collection; `String` elements are private copies so the source dict can be
freed right after the bind.  Strict `(T) d` throws `KeyException` on a missing
array field; lenient `(T)? d` leaves it at NULL.

Remaining gaps (Phase 3): `Map<K,V>*` fields (need `set(K,V)` dispatch) and
pointer-to-class elements (`List<User*>*` from a JSON array of nested objects).
Covered by `cy-validate/val-024-json-binding-collections.cy`.

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

### E1. (FIXED) Erasing the SAME class to two different interfaces failed codegen
Minimal repro (now compiles and runs):
```c
interface Shape     { double area(); }
interface Printable { String describe(); }
class Square { ... double area(); String describe(); ~Square(){} };

Any<Shape>*     a = any<Shape>(new Square(2.0));
Any<Printable>* b = any<Printable>(new Square(3.0));   // <-- used to fail here
```
**Old result:** `Repeated item declaration __thunk_dtor_Square` (MIR aborts).
The forwarding/destructor thunks (`__thunk_<m>_<Class>`, `__thunk_dtor_<Class>`)
are keyed on the concrete **class**, not the interface, but the dedup cache in
`synthesize_any_thunks` keyed only on the per-*(class, interface)* factory name
(`__any_make_<I>_<C>`). The second erasure re-emitted the same thunk
*definitions* → redeclaration.

**Fix (applied to `src/classyc.c`):** thunk definitions are now deduplicated by
their own per-class function name via `any_thunk_register_p()`. The first
erasure of a class emits the definitions; any later erasure (to a different
interface) emits only a **forward declaration** for each already-defined thunk,
so the new factory's references still resolve while MIR sees a single
definition. Shared method names across interfaces (e.g. two interfaces both
declaring `name()`) collapse onto the one per-class thunk as well. Validated by
`val-013-any-edge.cy` ("same class erased to interface #1 / #2").

**No workaround needed.** (Historically: erase through one interface, or declare
a single interface listing all methods.)

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
  `json()` parse/serialize round-trip, `d.json()` shorthand.
- dict JSON arrays at the value level: `d.nums` retrieves it, `json(d.nums)`
  -> `[10,20,30]`, integer subscript `d.nums[0]` returns the element.
- Automatic String arena: returns survive (release_keeping), tight 200k-alloc
  loops stay bounded (~18 MB RSS) and correct.
- The runtime stack-trace on faults (shows `main() [file:line]`).
- Typed JSON binding (`(T) d` / `(T)? d`): scalars, `String`, nested class/struct,
  and collection fields (`List<T>*` / `Set<T>*` from a JSON array, with owned
  private copies).  Strict throws `KeyException` on missing fields; lenient
  defaults to 0/NULL.  See `val-020` / `val-024`.
- Generic functions (`T Max<T>(T a, T b)`): call-site type inference,
  multi-parameter templates (`First<K,V>`), specialization cache.  See
  `val-023`.
- Stack value-construction with constructor arguments: `Point p = Point(1,2);`
  runs the ctor in place and `~Point()` at scope exit (RAII).
