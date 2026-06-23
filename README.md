# ClassyC — Modern C with Classes, Strings, Dicts, Lists & Maps

> **Note:** ClassyC now includes an LSP server and the `jitrunner` (hot-reload + DAP debug adapter) for a full development experience.

**ClassyC** is a C11 compiler with a carefully chosen set of modern language extensions that make systems programming feel dramatically more productive, while staying true to C's spirit. Classy is a heavily-modified c2m compiler from MIR.

It is built on the battle-tested [MIR](https://github.com/vnmakarov/mir) JIT/AOT infrastructure, giving you:
- Fast JIT execution (interpreter, lazy codegen, basic-block versioning)
- Real ahead-of-time compilation to native ELF object files (`b2obj`)
- The ability to ship standalone binaries or embed the compiler as a library

## Standout Features

### First-Class `String` (UTF-8)
```c
String greeting = "Hello, 世界 😊";
String name = "Ada" + greeting;
printf("%s\n", greeting + " from " + name);   // concatenation with auto-promotion

String s = "  Schöne Grüße  ";
s = s.trim().upper();                         // many built-in methods
size_t len = s.length();

String path = "/home/user/docs/report.pdf";
if (path.contains(".pdf"))
    path = path.replace(".pdf", ".txt");      // search-and-replace (every match)

// Methods work directly on a string literal, too:
printf("%s\n", (char*)"MiXeD".lower());        // -> mixed

// Split / join round-trips with List<String>
List<String> *parts = s.split(" ");
String rejoined = parts->join(", ");
```

> `replace` is overloaded: `replace(needle, repl)` is search-and-replace, while
> `replace(pos, len, repl)` is positional (handy with `find`, which returns a
> code-point **index** or `(size_t)-1`). Use `contains` for presence tests.
> Other methods: `substr(pos,len)`, `starts_with`, `ends_with`, `equals`,
> `empty`, `upper`/`lower`, `trim`. Methods may be called directly on a string
> literal (`"abc".upper()`) — no cast needed.

### Heterogeneous `dict` (JSON-like)
```c
dict cfg = {
    "server": { "host": "localhost", "port": 8080 },
    "debug": 1,
    "timeout": 30.5
};

printf("%s\n", (char*)cfg.server.host);       // string leaf -> cast to char*
int port = (int)cfg.server.port;              // numeric leaf -> read as a scalar
cfg.retries = 5;                              // dynamic key creation / assignment

for (auto k, v in cfg)
    printf("%s = %s\n", k, (char*)json(v));   // json() stringifies any value
```

> A `dict` value is a tagged box. Print a **string** leaf with a `(char*)` cast,
> read a **numeric** leaf with `(int)` / `(double)`, and use `json(v)` to
> stringify *any* value (object, array, number, or string).

JSON arrays come through too — parse, then index by position:

```c
dict d = json("{\"items\":[{\"name\":\"ada\",\"score\":42}]}");
printf("%s\n", (char*)d.items[0].name);       // "ada"
int score = (int)d.items[0].score;            // 42
```

`dict` also supports arena allocation (`new dict(bytes)`) and is the return type of `HttpResponse::asDict()`.

### Typed `Map<K, V>` Hash Maps (`include/map.h`)
The typed, type-safe sibling of `dict`: a generic open-addressing hash map that
fixes its key and value types at compile time, stores values inline (no boxing),
and works with any key type. String keys are hashed by **content**, scalars by
**value**, and objects (pointers) by **identity** — chosen at compile time via
`_Generic`, exactly like `Set<T>`.

```c
#include "map.h"

Map<String, int> *ages = new Map<String, int>();
ages["Ada"] = 36;                       // subscript write  ->  Set(key, val)
ages["Ada"] = ages["Ada"] + 1;          // subscript read   ->  Get(key)
if (ages->Contains("Ada")) { /* ... */ }

for (auto name, age in ages)            // (key, value) iteration, like dict
    printf("%s is %d\n", name, age);

// string -> object mapping
Map<String, Track*> *lib = new Map<String, Track*>();
lib["Kashmir"] = new Track("Kashmir", 508);
for (auto title, track in lib) track->play();

defer delete ages;
```

`Map<K, V>` plugs into the same language sugar as `List<T>` / `Set<T>`: subscript (`m[k]` / `m[k] = v`) lowers to `Get`/`Set`, and `for (auto k in m)` / `for (auto k, v in m)` iterate keys and key/value pairs in insertion order (the same `Count()` / `KeyAt(int)` / `ValAt(int)` protocol the compiler duck-types over). See `examples/classy-map.cy` for the full tour and `examples/classy-map-bench.cy` for a 100k-entry throughput benchmark.

### Generics, `List<T>`, `Set<T>` & Lambdas
Collections are reference types: allocate with `new`, call methods with `->`,
and brace-init with `new List<T>{ ... }`.

```c
#include "list.h"
#include "set.h"

List<int> *nums    = new List<int>{ 1, 2, 3, 4, 5, 6 };
List<int> *evens   = nums->Filter((int x) => x % 2 == 0);   // Filter -> new List
List<int> *doubled = evens->Map((int x) => x * 2);          // Map -> new List (chains)
defer delete nums; defer delete evens; defer delete doubled;

List<String> *files = new List<String>{ "a.txt", "b.pdf", "c.txt" };
List<String> *txt   = files->Filter((String f) => f.ends_with(".txt"));
defer delete files; defer delete txt;

List<Any<View>*> *widgets = new List<Any<View>*>();
widgets->Add(any<View>(new Button()));
widgets->Add(any<View>(new Text()));
for (auto v in widgets) v->render();   // heterogeneous via type erasure

// Set<T> — content-aware for String, identity for objects
Set<String> *tags = new Set<String>();
tags->Add("c"); tags->Add("c"); tags->Add("rust");
printf("unique tags: %d\n", tags->Count());   // 2
```

> `List<T>` provides `Filter`, `Map`, and `ForEach`. `Map` is a same-type
> transform (`T -> T`) that chains with `Filter`; for a cross-type (`T -> U`)
> map/filter/reduce pipeline, use the lowercase seq methods over a **C array or
> slice** that return a slice you can `.ToList()` (see the next section).

> **Element types & memory** — collections hold scalars, `String`, and pointers
> (e.g. `List<int>`, `Set<String>`, `List<MyClass*>`) directly. A `class` is a
> reference type, so put classes in by pointer (`List<Track*>`, `Set<Track*>`)
> via `new`. For the full picture — value-vs-pointer storage, the
> `Count()`/`Get(int)`/`Set(int,T)` protocol that powers `for-in` and `coll[i]`,
> how `Set<T>` hashes `String` by content but objects by identity, and the
> current limits on by-value class elements — see
> **[GENERICSMEM.md](GENERICSMEM.md)**.

#### Ownership: `.owns()` auto-frees pointer elements

A collection of pointers (`List<Track*>`, `Set<Track*>`, `Map<String, Track*>`)
stores the pointers but, by default, does **not** own the pointed-to objects —
`delete list` frees the container only. Add `.owns()` to make the collection the
owner: deleting it then runs each element's destructor and frees it. No manual
cleanup loop, no leaks.

```c
// OWNING: the list owns the Tracks; delete frees them all
auto library = new List<Track*>().owns();
library->Add(new Track("Kashmir", 508));
library->Add(new Track("Africa", 295));
defer delete library;                  // frees the list AND every Track ✅

// NON-OWNING (default): a view that shares another collection's objects
auto epics = library->Filter((Track* t) => t->seconds > 360);
defer delete epics;                    // frees the view's container only

// Set and Map have the same protocol:
auto favs = new Set<Track*>().owns();              // owns its elements
auto byId = new Map<int, Track*>().ownsValues();   // owns its values
//        Map also has .ownsKeys() and .owns() (both keys and values)
```

Rules of thumb:
- **Exactly one owner per object.** Make the collection that should free the
  objects `.owns()`; leave every sharing view at the default (non-owning).
- **Transform results are non-owning.** `Filter`, `Slice`, `Copy`, `Map`,
  `Union`, `Intersect`, `Difference` return views that share the source's
  elements — deleting them never double-frees.
- **By-value elements need nothing.** `List<Track>` (value, not pointer) already
  destroys its elements automatically via the `__destroy` intrinsic.

This works for any custom collection that follows the same `~Dtor` + element
loop pattern (it is powered by the `is_pointer<T>` compiler intrinsic and a
per-collection ownership flag); see `include/list.h`, `set.h`, and `map.h`.

### Arrays & Slices → `List<T>` (lengths flow into generics)
A C array or a filter/map slice converts to a heap `List<T>` with `.ToList()`,
or straight through the constructor. The compiler threads the source's length
(statically known for arrays, from the header for slices) alongside the bare
`T*`, so a single-argument constructor can recover it via `items.count()`:

```c
#include "list.h"

String names[] = { "alice", "bob", "carol" };

List<String> *l  = names.ToList();           // compiler supplies base + length
auto          l2 = names.ToList();           // `auto` deduces List<String>*
List<String> *l3 = new List<String>(names);  // same, via the constructor

int nums[] = { 1, 2, 3, 4, 5, 6 };
auto evens = nums.filter((int x) => x % 2 == 0).ToList();   // slice → List<int>
```

The array-view constructor takes just a `T*` and asks the pointer for its length:

```c
class List<T> {
    // ...
    List(T* items) {            // single-argument array-view constructor
        int n = items.count();  // length threaded in from the source array/slice
        // ... copy items[0..n) ...
    }
};
```

This is not special-cased to `List<T>`: any class collection whose constructor
(or method) takes a bare `T*` may recover the caller's element count with
`items.count()`, and call sites such as `new Bag<int>(arr)` fill it in
automatically.

### Classes with Constructors, Destructors & `new`/`delete`
```c
class Point {
    int x, y;

    Point(int x, int y) { this.x = x; this.y = y; }   // `this.` disambiguates the field from the parameter
    ~Point() { printf("~Point(%d,%d)\n", x, y); }

    Point* withX(int v) { x = v; return this; }       // bare field access; `this` is still the pronoun for chaining
    int sum() { return x + y; }
};

Point* p = new Point(3, 4).withX(10);         // heap + chaining
defer delete p;                               // RAII-style cleanup for heap memory
```

> `this.` on a field is **optional** inside method bodies — bare `x` resolves
> to the field. You only need `this.x` when a parameter or local of the same
> name shadows the field (as in the `Point` constructor above). `this` as a
> standalone pronoun (e.g. `return this;`) is unrelated and always available.

### `defer`, `delete`, and Scoped Resource Management
`defer` runs a statement on scope exit (LIFO, Go-style) — perfect for closing
files and freeing heap objects right where you acquire them.

```c
void process() {
    FILE* f = fopen("data.txt", "r");
    defer fclose(f);                 // runs last, on the way out

    auto cfg = new dict(64 * 1024);
    defer delete cfg;                // frees the whole arena on scope exit

    String report = "rows: " + 128;  // heap String, reclaimed automatically
    // ... no manual String cleanup needed (see Memory Management) ...
}
```

> Heap **Strings** are reclaimed for you automatically (see *Memory Management*)
> — there is no manual `checkpoint`/`release` API to call. Use `defer delete`
> for things you allocate with `new` (objects, arena dicts, collections).

### Arena Ownership: `unowned`, `detach`, `attach`

Three keywords act as explicit, readable operations on the per-scope cleanup
ledger that `defer` already implies. They're inverses of each other on the
same data structure:

| Keyword | When it runs | What it does |
|---|---|---|
| `defer` | end of scope | add a cleanup entry |
| `detach` | now, inline | remove an entry (escape) |
| `attach` | now, inline | add an entry (adopt) — *stub today* |
| `unowned` | at declaration | opt the binding out of future auto-cleanup |

```c
String build_label(int i) {
    return detach (String)"x#" + i;     // escape the arena: caller owns the value
}

Box* spawn(int v) {
    return detach new Box(v);            // ownership transfers to caller
}

class Request {
    String method;
    Request(String m) { method = detach m.trim().upper(); }   // store past scope
}

void handle() {
    unowned auto held = Http.get(url);   // I'll manage this one myself
    defer delete held;

    attach external_ptr;                 // (stub: parses + checks; no runtime call yet)
}
```

- **`detach <expr>`** is an expression. It evaluates the inner expression,
  removes the resulting value from the current scope's arena tracking set
  (`String` registry or object-handle registry), and yields the same value —
  now owned by whoever receives it. Works for `String` and pointer-to-class
  values; on a non-arena-tracked value (an integer, a `new`-allocated pointer
  the arena never tracked) it warns and falls through unchanged.
- **`unowned <decl>`** is a declaration prefix. Today it parses and is
  recorded in the AST as a no-op marker; it will become the opt-out for the
  upcoming auto-`defer delete` pass. Adding it now future-proofs your code.
- **`attach <expr>;`** is a statement. Today it's a stub (parses and
  type-checks; emits no runtime call). Reserved for the future
  ownership-flow / borrow-check pass that will use it to adopt
  externally-owned values into the current scope's arena.
- **Legacy `.detach()` method on `String`** still works for existing code
  (`examples/classy-controller-like.cy` uses it); the new keyword is the
  preferred form going forward and covers pointer-to-class values too.

Note: because `detach` is an expression-level keyword, it shadows any
ordinary identifier named `detach` in expression position (same rule
`new` follows). `attach` and `unowned` remain usable as identifiers in
expressions — they're only special at statement-start and
declaration-start respectively.

See `examples/test-ownership-keywords.cy` for a runnable demo.

### f-Strings (Interpolated Strings)
```c
String user = "bob";
int score = 42;
String msg = f"Hello {user}, your score is {score}";
printf(f"Score is {score}\n");
```

### Nice `auto` + Disambiguation
```c
auto x = 42;                    // int
auto d = {"name": "Ada", "age": 36};   // dict
auto arr = {1, 2, 3};           // int[3]
```

### `for (auto x in ...)` Loops
Works over arrays, `dict`, `List<T>`, `Set<T>`, `Map<K,V>`, and (via methods) strings. Keyed variant `for (auto k, v in m)` is supported for `dict` and `Map`.

### Interfaces & `Any<I>` Erasure
```c
interface Drawable { void draw(); }
class Circle impl Drawable { ... }

Any<Drawable> d = any<Drawable>(new Circle());  // erased handle
```

### Exceptions & Safety Guards (on by default)
```c
try {
    risky();
} catch (NullException e) {
    printf("null: %s\n", e.msg);
} catch (Exception e) {
    printf("other (id=%u): %s\n", e.id, e.msg);
}

throw(OutOfBoundsException, "bad index");
```

**On by default.** Exceptions *and* the JIT safety guards (null-deref,
divide-by-zero, array/slice out-of-bounds) are active unless you opt out with
`-fno-exceptions`. A guarded fault becomes a catchable exception:

```c
int *p = 0;
try { int v = *p; }                    // null-deref guard fires
catch (NullException e) { printf("caught: %s\n", e.msg); }
```

Built-in values: `NullException`, `OutOfBoundsException`, `RuntimeException`,
base `Exception`. No `#include` required.

**User-defined exceptions** work today without compiler changes:
```c
enum { MyKeyError = 100, MyParseError = 101 };   // IDs ≥ 100 conventional for users

try {
    ...
} catch (MyKeyError e) {
    ...
}
throw(MyKeyError, "key missing");
```
(See `examples/test-customexception.cy` and `examples/classy-exceptions.cy`.)

### HTTP/HTTPS Fetch (`include/httpclient.h`)
A header-only client to call a JSON API in one line. Responses come back the
classy way: `status` as an int, `headers` as a `dict`, `body` as a `String`,
and `asDict()` to parse JSON. HTTPS works out of the box (OpenSSL is loaded on
demand — nothing to link), and `List<String>` carries request headers.

```c
#include "include/httpclient.h"

void show_pokemon(String name) {
    String url = "https://pokeapi.co/api/v2/pokemon/" + name;
    auto   resp = Http.get((char *)url);
    defer delete resp;

    if (!resp->ok()) {
        printf("  %-12s  -> HTTP %d %s\n", (char *)name, resp->status,
               resp->error != NULL ? (char *)resp->error : (char *)resp->statusText);
        return;
    }
    dict d = resp->asDict();                  // JSON body -> dict
    printf("  #%d %s\n", (int)d.id, (char *)d.name);
}
```

See `examples/classy-fetch.cy` for the full tour (response headers, custom
request headers, batch fetch, 404 handling).

### Full C11 Base + Useful Extensions
- All standard C11 features (minus atomics/complex/VLA/TLS)
- Statement expressions, labels as values, range cases, binary literals, etc.
- Powerful MIR builtins for JIT specialization (`__builtin_prop_*`, `__builtin_jcall`, overflow helpers)
- Method overloading (resolved at compile time)

## How to Build

```bash
cd classyc
git submodule sync
cmake .             # builds in main dir, or into `build` dir
make                # builds the `classyc` (or `c2m`) compiler
```

The build also produces `b2obj` for ahead-of-time ELF object generation. (b2objmir on MacOS x64)

## Usage

### JIT Execution (fast iteration)
```bash
classyc example.c -eg               # generate machine code + run
classyc example.c -el               # lazy function generation
classyc example.c -eb               # lazy basic-block generation
classyc -g -c example.c -o a.bmir   # compile to bmir binary with debug info (link with `b2obj` / run with `jitrunner`)
```

### Ahead-of-Time Compilation
```bash
classyc -c example.c -o example.bmir  # emit MIR binary
b2obj example.bmir example.o          # produce native ELF .o
classyc-aot hello.c -o hello          # compile to native ELF binary script
```

You can link the resulting `.o` files with any standard C toolchain.

### As a Library
`classyc.c` (the single-file compiler) can be embedded exactly like the original c2mir. See the original c2mir documentation for the library interface.

### JIT Runner & Hot-Reload
The `jitrunner` (src/jitrunner/jitrunner.c) provides:

- Inotify-based hot reload on file change
- DAP debug adapter protocol for IDE integration
- Fork/exec isolation for safe recompilation

```bash
jitrunner --watch src/ --dap
```

An LSP server is also included for editor support (diagnostics, completion, go-to-definition).

## Examples

Look in the `examples/` directory:

| File                        | Highlights |
|----------------------------|------------|
| `classy.c`                 | Basic String + class usage |
| `classy-classes.c`         | `new`, constructors, fluent chaining, `delete` + `defer` |
| `classy-defer.c`           | `defer` ordering, early returns, destructors |
| `classy-dict.c`            | Full dict exercise (nesting, `in`, `for-in`, json round-trip) |
| `classy-dict-arena.c`      | Arena-allocated dicts |
| `classy-fstring.c`         | Interpolated f-strings |
| `classy-strings.c`         | All String methods |
| `classy-string-split-join.cy` | `String.equals`, `String.split` → `List<String>*`, and `List<String>.join` |
| `classy-auto.c`            | `auto` + dict/array disambiguation |
| `classy-generics.c`        | Generic `List<T>` (30 methods, brace-init `{a,b,c}`) |
| `classy-lambda.c`          | Typed lambdas for map/filter/sort/etc. |
| `test-list-stdlib.c`       | Full stdlib List<T> validation |
| `test-array-to-list.cy`    | Array/slice `.ToList()`, `auto` deduction, `List(T*)` ctor |
| `classy-sets.cy`           | Generic `Set<T>` hash set (content-aware `String` hashing) |
| `classy-map.cy`            | Generic `Map<K,V>` hash map (`m[k]`, `for (auto k,v in m)`, string→object) |
| `classy-map-bench.cy`      | `Map<K,V>` throughput benchmark (100k entries, int & String keys) |
| `classy-sets-myclass.cy`   | Custom `WordBag` class over `Set<T>`: word analytics (sort -u, set-grep, stop-words, Jaccard) |
| `classy-search-engine.cy`  | MapReduce inverted-index search engine over `List<T>` of custom classes |
| `classy-collections-class.cy` | `List<Track*>` + `Set<Track*>` over a custom class (Sort/Filter, set algebra by identity) |
| `classy-dict-init-class.cy`| Using `dict` inside class methods (with `this` calls) |
| `classy-overload.cy`       | Compile-time method overloading |
| `test-any-arena.c`         | `Any<I>` type erasure + arena-managed handles |
| `test-interface.c`         | `interface` + `impl` structural conformance |
| `test-any.c`               | Heterogeneous `List<Any<View>*>` (arena + non-arena) |
| `classy-exceptions.cy`     | `try`/`catch`/`throw` (opt-in via `-fexceptions`) |
| `classy-safety.cy`         | JIT safety guards: null-ptr, div-by-zero, array/slice OOB (auto-emitted with `-fexceptions`) |
| `classy-fetch.cy`          | HTTP/HTTPS client (`include/httpclient.h`): calls the PokéAPI over TLS, headers as a `dict`, `List<String>` |
| `test-customexception.cy`  | User-defined exceptions via `enum { MyErr = 100 }` |

Run them all with:
```bash
examples/run-examples.sh
```

## Memory Management

ClassyC manages high-level types with lightweight arenas. The big win: **heap
`String`s are reclaimed automatically** — there is no manual API to call.

- **String arena (automatic)** — every heap `String` (from `+`, `substr`,
  `replace`, `upper`/`lower`/`trim`, `split` …, *and* any helper / library
  call that returns a `String`, including `json(v)` and
  `List<String>.join(",")`) is tracked. The compiler emits a checkpoint at
  the start of each allocating function body **and at the top of each loop
  iteration** (`for`/`while`/`do`/`for-in`), and reclaims it at the bottom
  of the iteration / on `continue` / on `break` that exits the loop. A
  `String` you `return` is automatically kept alive for the caller. An
  `atexit` net guarantees a leak-free normal exit. You write no cleanup code
  — tight loops driven by helper calls (the `examples/classy-fetch.cy` HTTP
  fetcher pattern) stay bounded without manual `c2m_str_checkpoint`/
  `release_to` hooks. Caveat: if you assign a tracked `String` to a
  variable declared OUTSIDE the loop (`outerStr = helper(i);`), the
  compiler conservatively disables per-iteration release for that loop
  (the function-level scope still cleans up at return).
- **Object arena (automatic)** — `any<I>(...)` handles use the same scope-bound
  model and are reclaimed on scope exit (function or per-iteration); a
  handle you `return` is detached from the callee's arena and handed to the
  caller, who then owns it (`delete` it, or store it in a collection).
- **Dict arena (explicit)** — `new dict(bytes)` is arena-backed; `delete d`
  (or `defer delete d`) frees the whole arena and its contents in one shot.
- **Collections (explicit)** — `new List<T>` / `Map` / `Set` are heap objects you
  own; pair them with `defer delete`. For collections of pointers, add `.owns()`
  (`.ownsValues()` / `.ownsKeys()` on `Map`) and `delete` will also free the
  pointed-to objects — see *Element ownership* below.
- **Manual escape (`detach`)** — when you need a value to outlive the current
  scope (return it, store it in a long-lived class field, hand it to an outer
  collection), `detach <expr>` removes it from the local arena's tracking set
  while returning the same value. Pairs with `defer` as its inverse on the
  cleanup ledger. See *Arena Ownership* in the feature list above for the
  `unowned` / `detach` / `attach` keywords.
- **Static leak / UAF / double-free analyzer** — between check and gen the
  compiler runs a CFG-based forward dataflow over every function. Bindings
  initialized by recognized acquire calls (`malloc` / `calloc` / `realloc` /
  `strdup` / `strndup`) AND ClassyC `new T(...)` are tracked through a
  5-state ownership lattice (Unowned / Owned / Detached / Released /
  MaybeOwned) with null-check path narrowing on `if (p == NULL)` / `!p` /
  `p`.  `delete p;` releases the candidate (matching the language-level
  `new` → `delete` pair), `free(p)` releases malloc-family candidates,
  and `defer delete p;` / `defer free(p);` are recognized as scope-exit
  cleanup without invalidating subsequent reads.  The pass emits:
  - `warning: leak: ... is still owned at the end of this function`
  - `warning: potential leak: ... may be owned on some path` (MaybeOwned)
  - `error: use-after-free: ... was released earlier on this path`
  - `error: double-free` (and `warning: double-free risk` on loop back-edges)
  Per-arg hints via standard attributes on function parameters are
  understood: `__attribute__((borrows))` (read-only, do not retain),
  `__attribute__((releases))` (call takes ownership and frees), and the
  GCC `__attribute__((cleanup(fn)))` on a local variable suppresses the
  leak diagnostic (you've wired up RAII cleanup yourself).
- **`-fauto-release` — silently fix definite leaks.** When the analyzer is
  certain a binding leaks (Owned at every reachable function exit AND
  never observed escaping via return / store / detaching call), this flag
  has the compiler synthesize a `defer release_fn(p);` immediately after
  the declaration. The fix is invisible at the source level but runs
  through the existing defer machinery, so it unwinds at scope exit *and*
  on every `return` / `break` / `continue` path. MaybeOwned candidates,
  candidates that escape on any path, and bindings already marked
  `__attribute__((cleanup(fn)))` are skipped — synthesizing for them
  could double-free. Use `-v` to see each synthesized binding.
  ```bash
  classyc -fauto-release my-prog.cy   # turns five clean leaks into five free()s
  ```
- **`-fownership-report` — show what the analyzer verified.** Emits a
  structured per-function (and per-class, for methods) dump of every
  tracked allocation and *where* its ownership was disposed of — freed,
  returned to caller, stored into a non-tracked location, deleted,
  detached, auto-released, or leaked. Great for code review and for
  building trust in what the static checker proved:
  ```
  [ownership report]
  class Buffer
    fn Buffer::load  (foo.cy:18)
      tmp = malloc(...)  at foo.cy:19
        → freed by release fn  at foo.cy:21
    fn Buffer::grow  (foo.cy:25)
      fresh = malloc(...)  at foo.cy:26
        → stored into non-tracked location  at foo.cy:27
    fn Buffer::scratch  (foo.cy:31)
      junk = malloc(...)  at foo.cy:32
        → auto-released (-fauto-release)  at foo.cy:32
  fn make_name  (foo.cy:38)
    s = malloc(...)  at foo.cy:39
      → returned to caller  at foo.cy:41
  ```
  Combine with `-fauto-release` to see exactly which leaks the compiler
  is silently fixing for you.
- **Interprocedural summary inference** — the analyzer iterates over the
  whole TU until function summaries reach a fixpoint (capped at 4 silent
  passes; a final pass emits diagnostics).  For each function it infers:
  - per-parameter `((releases))` / `((borrows))` from how the body uses
    the parameter (releases on every reachable path → ((releases));
    untouched on every path → ((borrows));
  - whether the function returns an owned pointer + which release form
    callers should use (`returns_owned_p` + `returns_release_fn`).
  Call sites consult the inferred summary when no explicit annotation
  exists, so user-written wrappers like `void take(char *p) { free(p); }`
  are recognized as free-equivalents automatically.  A caller binding like
  `char *x = make_buf(...);` is auto-tracked when `make_buf` has the
  `returns_owned_p` summary, so leaks downstream of user wrappers are
  caught (and `-fauto-release` will silently insert a matching `defer`).
  Class-method calls treat the implicit `this` receiver as `((borrows))`
  by default — method calls don't escape the receiver.
  `-fownership-report` shows each inferred summary alongside the function
  header.
- **`-fcheck-whole-allocs` — link-time-style whole-program ownership.**
  Mirrors the spirit of `gcc -flto`: when you pass multiple `.cy` source
  files in one command, the driver stitches them into a single virtual TU
  (separated by `#line` directives so diagnostics keep their original
  filenames) and runs check + ownership + gen *once* over the combined
  AST.  The analyzer then sees every function definition from every file
  simultaneously and `-fownership-report` produces one unified dump.
  Pairs naturally with a single `-o foo.bmir` for a unified module.
  ```bash
  classyc -fcheck-whole-allocs -fownership-report \
          examples/test-whole-allocs.cy examples/test-whole-allocs-2.cy
  classyc -fcheck-whole-allocs -c -o app.bmir a.cy b.cy c.cy
  ```
  Caveat: a known c2mir preprocessor quirk under-counts newlines inside
  multi-line `/* ... */` block comments after a `#line` directive, so
  reports for files whose leading docstring is a multi-line block comment
  may have line numbers shifted by the comment's height.  Code and the
  ownership analysis itself are unaffected; only the *reported* line for
  diagnostics in that file shifts.  Workaround: use `//` line comments
  for top-of-file headers, or accept the shift.

Typical pattern:

```c
void handle() {
    auto cfg = new dict(256 * 1024);
    defer delete cfg;                 // explicit: you own `new`

    auto names = new List<String>();
    defer delete names;

    String greeting = "hi " + cfg.user;   // heap String — reclaimed automatically
    // ... no String cleanup needed ...
}
```

### Element ownership (`.owns()`)

A pointer collection stores the pointers but, by default, does not own the
objects they point to — `delete list` frees only the container. Mark it `.owns()`
to transfer ownership: deleting the collection then runs each element's
destructor and frees it, with no manual cleanup loop.

```c
auto library = new List<Track*>().owns();   // owns the Tracks
library->Add(new Track("Kashmir", 508));
defer delete library;                       // frees the list AND every Track ✅
```

- **One owner per object.** Make the owning collection `.owns()`; leave sharing
  views (and the results of `Filter`/`Slice`/`Copy`/`Union`/… ) at the default
  non-owning state. Transform results are always non-owning, so they never
  double-free a shared element.
- **`Map`** distinguishes value vs. key ownership: `.ownsValues()`,
  `.ownsKeys()`, or `.owns()` for both.
- **By-value elements** (`List<Track>`, not `List<Track*>`) are destroyed
  automatically via the `__destroy` intrinsic — no `.owns()` needed.
- **Custom collections** get this for free by following the same destructor
  pattern; it is powered by the `is_pointer<T>` compiler intrinsic plus a
  per-collection ownership flag (see `include/list.h`, `set.h`, `map.h`).

See `cy-validate/` for executable tests of the String arena (a 200k-allocation
loop stays bounded), the object/dict arenas, and the `.owns()` protocol
(`val-017-collections-owns.cy`, `val-018-owns-transforms.cy`).

## AOT Compilation

`b2obj` now emits basic [DWARF v4](https://dwarfstd.org/) debug information:

```bash
classyc -c -g foo.cy -o foo.bmir
b2obj --dwarf4 foo.bmir foo.o
gcc -g -o foo foo.o
gdb foo 
```

Load the resulting object in GDB or any DWARF-aware debugger to step through ClassyC source with line information.

## Architecture

ClassyC retains the clean four-pass design of c2mir:

1. Preprocessor → tokens
2. PEG-style manual parser → AST
3. Semantic checker (types, scopes, classes, dicts, String methods)
4. MIR code generator (with heavy lowering for String/dict/class features)

New language constructs (`CLASS`, `DICT`, `STRING`, `N_NEW`, `N_DEFER`, `N_FORIN`, f-strings, etc.) are handled with the same disciplined style as the original compiler.

The runtime support for String methods and dict operations lives in small C helpers that are automatically imported during code generation.

## Status & Future

ClassyC is a pragmatic, evolving experiment in "C but pleasant". It already delivers a delightful developer experience for data-heavy systems code (proxies, config-driven services, CLIs, embedded scripting).

Shipped since the early roadmap: typed lambdas, generics (`List<T>` and
user-defined collections), `interface`/`Any<I>` erasure, default-on exceptions +
safety guards, and array/slice → `List<T>` conversion with lengths flowing into
generics. In-progress directions include richer container types and broader
standard-library coverage.

The behavior described in this README is exercised by the executable validation
suite in **[`cy-validate/`](cy-validate/)** (run `sh cy-validate/run-validate.sh`).
Known rough edges and their workarounds are catalogued in
**[`cy-validate/SHORTCOMINGS.md`](cy-validate/SHORTCOMINGS.md)**.

Contributions, bug reports, and wild ideas are welcome!

---

## Limitations & Future Work

### What doesn't work / current limitations
- Single inheritance (`extends` / `super` / `virtual` methods). Use `interface` + `impl` + `Any<I>` (structural typing) instead — this combination covers all observed use-cases in ~8600 lines of examples.
- Class instances stored **by value** inside `List<T>`, `Set<T>`, and `Map<K,V>` are supported: elements (and `Map` keys/values) live inline and the collection runs each element's destructor on `delete`. Scalars, `String`, raw pointers, and `MyClass*` work as before. For **pointer** elements the collection is non-owning by default, but `.owns()` (`.ownsValues()`/`.ownsKeys()` on `Map`) makes `delete` also free the pointed-to objects. (See `GENERICSMEM.md` and the *Element ownership* section above.)
- `dict` arrays: JSON parsing builds them and `d.arr[i]` reads elements, but **array-literal assignment** (`d.tags = [..]`) is unimplemented and `for-in` does **not** iterate a dict array value (index by position instead).
- Stack value-construction works for plain classes: `Point p = Point(1, 2);` runs the constructor in place and `~Point()` at scope exit. It is the **generic collections** (`List<T>` / `Set<T>` / `Map<K,V>`) that are reference types only — instantiate them with `new` (a bare `Map<K,V> m = ...` value expression does not parse).
- Exception names are resolved only at compile time. Runtime stores integer IDs only; there is no symbolic pretty-printing or `nameof`-style reflection for exceptions.
- No built-in "key not found" exception type for `dict`/`Map` subscript (users can define their own via `enum { MyKeyError = 100 }` and `throw`/`catch` it manually).
- `List<T>.sort` / `Set<T>` and a few other methods have minor edge-case limitations documented in the headers.

### Want-to-have features (prioritized)
- ~~Automatic `defer delete` for `new`-bound locals, with `unowned` as the opt-out~~ **(landed)** — the static ownership analyzer in `src/ownership.c` tracks `malloc`-family and `new`-bound locals through a 5-state lattice, and `-fauto-release` synthesizes `defer free(p);` for definite leaks (see *Memory Management*). `unowned` is the opt-out at the declaration site.
- A working `attach <expr>;` paired with a lightweight dataflow / borrow-check pass: today `attach` parses and type-checks but emits no runtime call. Once the analysis is in, `attach` will adopt an externally-owned value into the current arena and the compiler will be able to prove every owning binding is matched by exactly one of `{scope-end defer, detach, attach-into-another-scope}`.
- Richer `List<T>` / `Map<K,V>` syntactic sugar and initializer syntax (more Pythonic comprehensions, better literal support, safer typed JSON deserialization into `List<T>` / `Map<K,V>`).
- Safe / typed JSON parsing helpers that return `Result<T, ParseError>` or throw on failure (beyond the current `asDict()` which can produce a null-ish dict on bad input).
- Lightweight SQLite wrapper (`include/sqlite.h`) with automatic binding of `dict` rows and `List<dict>` result sets — a natural fit given the existing `dict` infrastructure.
- Simple gunicorn-style HTTP server library (lower priority than SQLite).
- Optional pretty-printing / symbolic names for user-defined exceptions at debug time.

*Built with ❤️ on top of MIR. Original c2mir design by Vladimir Makarov.*
