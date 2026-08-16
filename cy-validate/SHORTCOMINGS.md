# ClassyC — Shortcomings & Gotchas (validation findings)

Real, current **language/compiler** shortcomings and gotchas, each with a
**workaround** and a runnable code sample. Living document — fixed items
move to the "Fixed" list below as a one-line pointer to their regression
test, not a postmortem.

Compiler/runtime tested via: `./bin/classyc -g -I include <file>.cy -eg`.
Everything here is exercised by `cy-validate/` (`sh cy-validate/run-validate.sh`);
README code snippets are kept in sync with this file.

---

## Current gotchas

### 1. The String arena has no manual API
```c
String arena = String.checkpoint();   // compile error: no such static method
defer String.release_to(arena);
```
String arena reclamation is fully automatic (per-function and per-loop-iteration
checkpoints, emitted by the compiler). There is no user-facing surface — delete
calls like this and just allocate normally:
```c
String build(int i) {
    String s = (String)"item-" + i;   // tracked automatically
    return s;                          // survives the caller's release
}
int main() {
    for (int i = 0; i < 3; i++) printf("%s\n", (char*)build(i));
    return 0;                          // -> item-0 / item-1 / item-2, no leak
}
```
Validated in `val-002-string-arena.cy`.

### 2. `if (s.find(x))` is a buggy idiom — use `contains()`
`find` returns a code-point index, or `(size_t)-1` when not found. `(size_t)-1`
is truthy (looks "found"); a match at index 0 is falsy:
```c
String s = "hello world";
if (s.find("hello")) { /* never runs */ }        // BUG: match at 0 is falsy
if (s.contains("hello")) { /* runs */ }           // correct
if (s.find("xyz") != (size_t)-1) { /* skipped */ } // correct, and gives the index
```

### 3. `printf("%s", dict_value)` is rejected or unsafe
```c
dict cfg = { "host": "localhost", "timeout": 30 };
printf("%s\n", cfg.host);            // compile error (dict is non-pointer)
printf("%s\n", (char*)cfg.timeout);  // compiles, segfaults — timeout is numeric
```
A dict dot/subscript yields a tagged `dict` value; casting a *numeric* leaf to
`char*` and printing it dereferences garbage.

**Workaround:** stringify with `json()`, or cast only a leaf you know is a string;
read numeric leaves with `(int)`/`(double)`:
```c
printf("%s\n", (char*)json(cfg));   // {"host":"localhost","timeout":30}
printf("%s\n", (char*)cfg.host);    // "localhost" — safe, host really is a string
int t = (int)cfg.timeout;           // 30 — read the numeric leaf as a scalar
```
For a value whose shape you don't know ahead of time, check the runtime tag
first instead of guessing — see gotcha #4.

### 4. (GUARDED) Don't `#include "dict.h"` from user code
`dict.h` is the compiler/runtime's own implementation (full `DictValue` struct,
JSON parser, BSON codec) — not a user-facing API. Casting a `dict` value to
`DictValue*` and reading a field directly looks plausible but is wrong: every
`dict`→pointer cast in `.cy` code unwraps to the union payload (the scalar at
offset 8), so `((DictValue*)v)->type` reads payload bytes, not the real tag.

`dict.h` now guards itself: it only compiles when `DICT_CLASSYC_INTERNAL` is
defined first (the compiler's own C sources define it before including), so a
plain `.cy`-side include fails with one clear message instead of a wall of
confusing ownership-checker findings against the runtime's own internals:
```c
#include "dict.h"
int main() { return 0; }
```
```
include/dict.h:1350:2: error -- #error "dict.h is ClassyC's internal runtime
header, not for .cy user code; #include \"dict_types.h\" and use d.type()
instead (see cy-validate/SHORTCOMINGS.md)"
```

**Workaround:** include `dict_types.h` instead — it exports just the
`DictType` enum for use with the `d.type()` builtin:
```c
#include "dict_types.h"
dict cfg = { "host": "localhost", "timeout": 30 };
switch (cfg.host.type()) {
    case DICT_STRING: printf("host: %s\n", (char*)cfg.host); break;
    default: break;
}
switch (cfg.timeout.type()) {
    case DICT_INT64:  printf("timeout: %d\n", (int)cfg.timeout); break;
    case DICT_NUMBER: printf("timeout: %f\n", (double)cfg.timeout); break;
    default: break;
}
```
(If you truly need the runtime internals — e.g. hand-rolling a serializer —
`#define DICT_CLASSYC_INTERNAL` before the `#include`, plus `-fno-ownership`
for the checker findings against `dict.h`'s own code; you're on your own for
safety at that point.)

### 5. `+` concat needs a `String`-typed left operand
`"tmp" + i` is C pointer arithmetic (`char* + int`), not concatenation:
```c
int i = 2;
char *bad = "hello" + i;             // pointer arithmetic -> "llo"
printf("%s\n", bad);

String good1 = (String)"hello" + i;  // forces concatenation -> "hello2"
String base  = "hello";
String good2 = base + i;             // base is already String -> "hello2"
```

### 6. `dict` array literals work in an initializer, not in a bare assignment
```c
dict d = { "tags": ["fast", "safe"] };   // works
d.tags = ["fast", "safe"];               // syntax error
```
`[e1, e2, …]` is only recognized inside `P(initializer)`; an assignment
expression never reaches that parse path.

**Workaround:** build the array into the initializer, or go through JSON:
```c
dict d = json("{\"tags\":[\"fast\",\"safe\"]}");
```

### 7. `List<T>.Map` is same-type only (`T → T`)
`List<T>* Map(T(*fn)(T))` chains with `Filter`/`ForEach` but can't change the
element type (`nums.Map((int x) => f"{x}")` to get a `List<String>*` from a
`List<int>*` does not compile).

**Workaround:** use the lowercase seq methods over a C array/slice instead —
they allow a type change through `.map()`:
```c
int arr[] = {1, 2, 3, 4};
List<String> *strs = arr.filter((int x) => x > 1)
                         .map((int x) => f"n{x}")
                         .ToList();
for (auto s in strs) printf("%s\n", (char*)s);   // n2 / n3 / n4
```

### 8. Typed JSON binding doesn't fill `Map<K,V>*` fields
```c
class Config { Map<String,int> *scores; };
Config c = (Config) d;   // error: dict->collection bind: '...' has no Add(T) method
```
Scalars, `String`, nested class/struct, and any `Add(T)`-protocol collection
(`List<T>*`, including pointer-to-class elements) bind automatically; `Map<K,V>*`
needs `set(K,V)` dispatch that isn't implemented yet. This fails loudly at
compile time — no silent corruption.

**Workaround:** walk the dict object yourself and populate the `Map`:
```c
#include "map.h"
dict d = json("{\"scores\":{\"alice\":90,\"bob\":75}}");
dict scores = d.scores;

auto m = Map<String, int>();
for (auto key, val in scores)      // key is char*, val is dict
    m[(String)key] = (int)val;

printf("alice=%d bob=%d\n", m["alice"], m["bob"]);
```
See `val-024-json-binding-collections.cy`.

### 9. What still doesn't run across a `throw`: RAII stack dtors, non-`delete` defers, non-class deletes
A `throw` lowers to `cy_exc_throw()` + `longjmp` straight to the enclosing
`try`'s `setjmp` point, bypassing every *syntactic* exit point
(`return`/`break`/`continue`/fall-through) where cleanup code is normally
emitted. Three of the four cleanup mechanisms are now covered across that
jump — the String and `Any<I>` arenas (banked marks), explicit
`defer delete <class-ptr>;`, and `owned` class bindings (a runtime shadow
stack of cleanup thunks, `cy__defer_stack` in `include/cyexc.h`: registration
pushes `(thunk, ptr)`, normal exits discard as they replay, the exception-
dispatch path invokes everything pushed since the catching try's entry —
works across any number of unwound call frames):
```c
try {
    auto x = new Box(1);
    defer delete x;                       // now runs across the throw
    owned auto b = new Box(2);            // so does this
    throw(RuntimeException, "oops");
} catch (Exception e) { /* ~Box ran for both */ }
```
Regression test: `cy-validate/val-062-defer-throw.cy`.

**Still open:**

- **RAII stack-object destructors** — `Point q = Point(1, 2);` inside a try
  body (or an unwound frame) still never runs `~Point` on the exception path.
  The shadow stack captures its argument by value at registration; a captured
  *stack address* is a dead-frame pointer once `longjmp` has unwound past it,
  so running the destructor through it would corrupt rather than clean up.
  This one genuinely needs real stack unwinding.
- **Arbitrary `defer` bodies** — only `defer delete <class-ptr-expr>;` is
  shadow-registered. `defer fclose(f);`, `defer free(p);`, `delete` of a
  non-class pointer, etc. are still skipped across a throw.
- **The deferred pointer is captured at registration** (Go-style), not read
  back at scope exit. Reassigning the variable between the `defer` and the
  `throw` cleans up the *registered* value:
  ```c
  Box *x = new Box(1);
  defer delete x;
  x = new Box(2);      // normal exit deletes Box(2); a throw deletes Box(1)
  ```
- **`owned` + `move` + a later `throw` can double-free**: the shadow entry
  holds the pre-`move` pointer, and the moved-to owner releases it too. Don't
  mix `move` with code that can throw into an enclosing try.
- **An `owned` binding declared after a try/catch in the same function** is
  a separate, pre-existing ownership-pass bug (marked `Unowned`, leaks even
  without a throw) — see `bugs/013-owned-after-try.cy`.

**Workaround (for everything still open):** clean up explicitly inside the
`catch` (which *does* run normally) instead of relying on `defer`/`owned`
/RAII for anything allocated before a possible `throw`:
```c
Box *x = new Box(1);
try {
    throw(RuntimeException, "oops");
} catch (Exception e) {
    printf("caught: %s\n", (char*)e.msg);
    delete x;      // manual cleanup here runs correctly
}
```
See `DOC/FOOTGUNS_AND_IMPROVEMENTS.md` for the fuller writeup.

---

## Fixed

Each item below used to be broken; it isn't anymore. One line + the
regression test that proves it — see git history / `src/classyc.c` if you
need the "why."

- **Cross-function String arena reclamation** — a helper returning `String`
  (incl. `json()`, `list->join()`) used to leave the caller's loop unbounded.
  Now automatic, layered with a per-iteration loop scope. `val-019-loop-arena.cy`.
- **Struct-hack / flexible-array-member OOB guard** — `T a[1]` trailing arrays
  on heap structs (SQLite's `ExprList` shape) used to over-trap the runtime
  OOB guard. Bound is now inferred from a sibling capacity field.
  `val-057-flex-array.cy`.
- **`#include "list.h"` / `"map.h"` / `"set.h"` needing `-I include`** —
  `classyc` now auto-discovers its own `include/` next to the binary.
- **`#include "dict.h"` from user code compiling into a wall of confusing
  ownership-checker errors** — `dict.h` now gates its whole body on
  `DICT_CLASSYC_INTERNAL` and fails with one clear message instead. See
  current gotcha #4 for the intended replacement (`dict_types.h` + `d.type()`).
- **`replace(needle, repl)` search-and-replace** — now an overload alongside
  the positional `replace(pos, len, repl)`. `val-015-string-literal-and-replace.cy`.
- **String methods on a string-literal receiver** — `"MiXeD".lower()` etc. now
  dispatch without a cast. `val-015-string-literal-and-replace.cy`.
- **Stack/value `List<T>` / `Set<T>` / `Map<K,V>`** — `auto xs = List<int>();`,
  move-only transfer, brace-init all work; heap `new` form still works too.
  `val-038`–`val-040`.
- **Stack class instances with constructor arguments** — `Point p = Point(1,2);`
  runs the ctor in place, `~Point()` at scope exit.
- **Array literals nested inside a dict initializer** — `{ "powers": [1,2,3] }`
  now parses and binds (bare assignment still doesn't — see gotcha #6).
- **`d.length()` / `d.count()`** — expose array length / pair count as methods
  (don't collide with real dict keys of the same name).
- **Deep dict-leaf access** — `d.items[0].value` stays a tagged `DictValue*`
  instead of silently unwrapping (which used to segfault on a bare read);
  cast explicitly to read a scalar.
- **`for-in` over a dict array** — dispatches correctly on the runtime
  `DictType` tag for both single- and two-variable forms. `val-004-dict-arrays.cy`.
- **Typed JSON binding into `List<ClassPtr>*`** (e.g. `List<User*>*`) — used to
  compile silently and hand back corrupted element pointers that crashed on
  first use (`scalar_type_p()` swallowed the pointer-to-class case before it
  reached dedicated handling). Now allocates and binds real objects, including
  through nested by-value members. `val-058-json-bind-list-classptr.cy`.
- **Returning an `Any<I>*` handle from a function** — used to be a
  use-after-free (arena released the handle before the caller read it). Now
  detached from the arena before release, so the caller owns it.
  `val-013-any-edge.cy`, `val-014-any-return-mem.cy`.
- **`Any<I>*` handles retained in a collection** — a collection returned from
  a function, or built by adding a freshly-erased handle inside a loop, used
  to have its elements freed by the (per-iteration or function-level) object
  arena while the collection still held them — a use-after-free needing no
  `return` at all to trigger. `subtree_retains_object_in_collection_p` now
  detects a handle passed into a generic collection's method call and
  disables automatic reclamation for that scope; such a collection needs
  `.owns()` / `.ownsValues()` (or manual per-element `delete`) like any other
  pointer collection. `val-059-any-collection-escape.cy`. (Uncovered a
  separate atexit-sweep crash in the process, since fixed — see the
  "leaked `Any<I>*` handle crashes at process exit" entry below.)
- **Erasing the same class to two different interfaces** — used to abort MIR
  generation with a duplicate-thunk redeclaration. Thunk definitions are now
  deduplicated per class, with forward declarations for later erasures.
  `val-013-any-edge.cy`.
- **String / `Any<I>*` arena cleanup skipped by `throw`** — see gotcha #9:
  a `try` block now banks its String/object arena marks into `cy_exc`'s own
  frame stack at entry and releases back to them on the exception-dispatch
  path, so heap Strings and `Any<I>*` handles allocated before a caught
  `throw` are correctly freed instead of leaked.
- **Explicit `defer delete` / `owned` cleanup skipped by `throw`** — a
  runtime shadow stack of per-class cleanup thunks (`cy__defer_stack` in
  `include/cyexc.h`): `defer delete <class-ptr>;` and `owned` class bindings
  push `(thunk, ptr)` at registration (pointer captured by value), normal
  exits discard the entry as they replay the AST, and the exception-dispatch
  path invokes everything pushed since the catching try's entry — across any
  number of unwound call frames. `cy-validate/val-062-defer-throw.cy`.
  Remaining limits (RAII stack dtors, non-`delete` defer bodies, non-class
  deletes) are in gotcha #9.
- **A leaked `Any<I>*` handle crashed at process exit instead of just
  leaking** — the object arena's `atexit()` safety net (`c2m_obj_cleanup` in
  `include/cobjarena.h`) called each leaked handle's destructor thunk
  *after* `classyc-driver.c` had already unloaded the JIT-generated code
  (`MIR_gen_finish`/`MIR_finish` run before the real process `exit()` that
  triggers libc's `atexit` chain), so the call jumped into unmapped memory.
  The driver now calls `exit()` immediately after execution finishes, in all
  four exec modes (`-ei`/`-eg`/`-el`/`-eb`), while the generated code is
  still live. `val-061-atexit-leak-cleanup.cy`.
- **No compile-time signal for a pointer-collection missing `.owns()`** —
  `List<T*>`/`Set<T*>`/`Map<K,T*>` built up via `Add()`/`Set()` with fresh
  `new`/`any<I>()` elements used to compile silently with no ownership
  acknowledgment at all — exactly the shape that crashed
  `examples/test-any-arena.cy` and `examples/test-any-implicit.cy` at exit.
  `ownership.c` now warns when such a collection never calls
  `.owns()`/`.ownsValues()`/`.ownsKeys()` (chained or separate), and never
  has its elements deleted by hand either. `val-060-owns-warning.cy`.

---

## Also confirmed solid

Not shortcomings — spot-checks worth knowing hold up, mostly so you don't
have to wonder:

- `dict`: nested init, dot/subscript read-write, dynamic keys, `"k" in d`,
  `for (auto k[, v] in d)`, `json()` round-trip.
- Interface conformance checking is sound in both directions (`class C impl I`
  and `any<I>(new C(...))` both reject a class missing a method; a handle only
  exposes the interface surface, not the concrete class's extra methods).
- Non-trivial erased dispatch through `Any<I>*` — non-`void` returns, methods
  with arguments, mutation through the handle, passing/returning a handle,
  storing single handles in `List<Any<I>*>` / `Map<K, Any<I>*>`.
  `val-012-interfaces-any.cy`, `val-013-any-edge.cy`.
- Generic functions (`T Max<T>(T a, T b)`): call-site inference,
  multi-parameter templates, specialization cache. `val-023-generic-fns.cy`.
- The runtime stack trace on faults resolves source locations
  (`main() [file:line]`), not just addresses.
