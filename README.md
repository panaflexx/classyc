# ClassyC

ClassyC is a **C11 compiler** with a small set of extensions that make
everyday systems code less painful: classes, UTF-8 `String`, JSON-like
`dict`, generic `List` / `Map` / `Set`, and a JIT that runs your program
the moment it compiles.

It is a heavily modified [c2m](https://github.com/vnmakarov/mir) front
end on [MIR](https://github.com/vnmakarov/mir). You can interpret, JIT,
or AOT to a native binary. Ordinary C still compiles.

```bash
./bin/classyc -I include examples/readme-taste.cy -eg
```

```
4 tracks, 2 of them stretch out
  Led Zeppelin — Kashmir (508s)
  Pink Floyd — Echoes (1412s)
mood=late  artists=4
```

That file is `examples/readme-taste.cy`. It is a real program, not a
sketch.

---

## What you get

- **C11**, minus complex numbers. Variable-length arrays are limited;
  trailing flexible arrays (`T a[1]` / `T a[]`) work. Atomics and
  `_Thread_local` are supported.
- **Classes** with constructors, destructors, `new` / `delete`, and
  `this`.
- **`String`** — UTF-8, `+` concatenation, methods on values *and*
  literals (`"MiXeD".lower()`).
- **`dict`** — JSON-shaped maps and arrays, plus `(User) d` to bind a
  dict onto a class or struct.
- **`List<T>` / `Map<K,V>` / `Set<T>`** as stack values. Pipelines
  return values. Prefer that over `new` / `delete` for locals.
- **Lambdas** that stay C-shaped: a non-capturing `=>` is a function
  pointer; a capturing one is inlined only as a collection callback.
- **Exceptions and safety traps on by default** (null, divide-by-zero,
  OOB). Opt out with `-fno-exceptions`.
- **JIT and AOT** on the same front end. An LSP (`bin/classyc-lsp`)
  and a DAP runner (`bin/jitrunner`) are in the tree.

---

## Build

`ext/mir` is a git submodule. Clone with `--recurse-submodules`, or:

```bash
git submodule update --init ext/mir
cmake -B build -S .
cmake --build build -j
```

In-tree `cmake . && make` also works. Either way the compiler lands at
`bin/classyc`. Linux gets `bin/b2obj` (ELF); macOS builds `b2objmac`
and aliases it as `b2obj`.

---

## Run

```bash
# JIT-compile and run (this is the usual loop)
./bin/classyc -I include examples/readme-taste.cy -eg

# Same, but lazy: generate a function the first time it is called
./bin/classyc -I include examples/readme-taste.cy -el

# Compile to binary MIR, then a native object, then a binary
./bin/classyc -I include -c examples/readme-taste.cy -o /tmp/taste.bmir
./bin/b2obj /tmp/taste.bmir /tmp/taste.o
# or, one shot:
./classyc-aot.sh -I include examples/readme-taste.cy -o /tmp/taste
```

`-I include` is how `list.h`, `map.h`, `set.h`, `chan.h`, and friends
are found. Quoted includes also search the current directory, so some
examples write `#include "include/httpclient.h"` and still work from
the repo root.

| Flag | What it does |
|------|----------------|
| `-eg` | generate machine code and run |
| `-el` | lazy per-function generation |
| `-eb` | lazy per-basic-block generation |
| `-ei` | interpret MIR |
| `-c` / `-S` | emit `.bmir` or textual MIR |
| `-g` | source locations for gdb / jitrunner |
| `-On` | MIR + midopt level (`-O2` is the default) |
| `-ffibers` | enable `go` / `await` |
| `-fno-exceptions` | no exceptions, no safety traps |
| `-l name` / `-L dir` | same idea as `cc` (`-l sqlite3`, not a path) |

`classyc` is also a C compiler. The MIR c-test suite is
`sh ext/mir/c-tests/runtests.sh ext/mir/c-tests/use-c2m-gen-O3 bin/classyc`.

---

## A first program

`examples/readme-taste.cy` — a record crate, not a tutorial in
disguise.

```c
#include <stdio.h>
#include "list.h"
#include "map.h"

[[copyable_no_release]]
class Track {
    String title;
    String artist;
    int    seconds;

    Track (String title, String artist, int seconds) {
        this.title = title;
        this.artist = artist;
        this.seconds = seconds;
    }
    ~Track () {}

    int IsLong () { return seconds >= 360; }

    String Label () { return f"{artist} — {title}"; }
};

int main (void) {
    auto crate = List<Track> ();
    crate.Add (Track ("Kashmir", "Led Zeppelin", 508));
    crate.Add (Track ("Africa", "Toto", 295));
    crate.Add (Track ("Tom Sawyer", "Rush", 276));
    crate.Add (Track ("Echoes", "Pink Floyd", 1412));

    auto epics = crate.Where ((Track t) => t.IsLong ());
    printf ("%d tracks, %d of them stretch out\n", crate.Count (), epics.Count ());

    for (auto t in epics)
        printf ("  %s (%ds)\n", t.Label (), t.seconds);

    auto counts = Map<String, int> ();
    for (auto t in crate)
        counts[t.artist] = counts.GetOr (t.artist, 0) + 1;

    dict night = { "mood": "late", "volume": 7 };
    printf ("mood=%s  artists=%d\n", (char *) night.mood, counts.Count ());
    return 0;
}
```

A few things that file is quietly showing:

- A **class** is a semicolon-terminated definition, like a struct.
- **`this.field`** is only required when a parameter shadows the field.
  Bare `seconds` in `IsLong` is the member.
- **`[[copyable_no_release]]`** marks a class whose destructor does not
  free a unique resource. `List<Track>` stores `Track` by value and
  relocates elements with `memcpy`, so a destructor that `free`s would
  be a double-free. `String` fields are fine under this attribute;
  unique heap resources belong in `List<T*>` with `.owns()`.
- **`auto crate = List<Track>()`** is a stack shell. `~List` frees the
  buffer at the `}` of `main`. `Where` returns another stack `List`.
- **`f"{artist} — {title}"`** is an f-string.
- **`dict`** is a tagged JSON value. A string leaf prints with
  `(char *)`; a number leaf reads with `(int)` / `(double)`.

---

## Strings

`String` is a built-in UTF-8 type. `length()` counts **code points**,
not bytes. `"héllo".length()` is 5.

```c
String s = "  Schöne Grüße  ";
s = s.trim().upper();                    // SCHÖNE GRÜSSE
size_t n = "hello world".find("world");  // 6, or (size_t)-1
String path = "report.pdf".replace(".pdf", ".txt");

List<String> *parts = "a,b,c".split(",");
String back = parts->join("|");          // "a|b|c"

printf("%s\n", (char *)"MiXeD".lower()); // mixed
```

`replace(needle, repl)` is search-and-replace. `replace(pos, len, repl)`
is positional — handy next to `find`. Also: `substr`, `starts_with`,
`ends_with`, `contains`, `equals`, `empty`.

`+` concatenates and will promote numbers and other scalars:
`"rows: " + 128`. Heap strings are tracked and reclaimed for you; see
[Memory](#memory).

---

## dict and JSON

A `dict` is a tagged box: object, array, number, string, bool, or null.

```c
dict cfg = {
    "server": { "host": "localhost", "port": 8080 },
    "debug": 1,
    "logfiles": ["access.log", "error.log"]
};

printf("%s\n", (char *)cfg.server.host);
int port = (int)cfg.server.port;
cfg.retries = 5;                         // new key

dict d = json("{\"items\":[{\"name\":\"ada\",\"score\":42}]}");
printf("%s\n", (char *)d.items[0].name); // ada
int n = (int)d.items.length();           // 2  (alias: .count())

for (auto i, item in d.items)
    printf("%d: %s\n", i, (char *)item.name);
```

`for-in` looks at the runtime tag: an object yields `(key, value)`, an
array yields `(index, element)`. `json(v)` stringifies any box.

Array-*literal* assignment (`d.tags = ["fast"];`) is not implemented.
Build arrays with JSON, or with `dict_create_array` / `dict_array_append`.

### Bind a dict onto a type

```c
class Address { String city; int zip; };
class User    { String name; int age; Address addr; };

dict d = json(req.body);
User u  = (User) d;     // missing field → KeyException
User u2 = (User)? d;    // missing fields stay 0 / NULL
```

That walk is driven by the declared members. Names must match keys
exactly. Scalars, `String`, nested class/struct values, and any
`Add(T)`-protocol collection field (`List<T>*` / `Set<T>*`, including
pointer-to-class elements like `List<User*>*`) all work. `Map<K,V>*`
fields are not bound yet.

`cy-validate/val-020-json-binding.cy`,
`val-024-json-binding-collections.cy`, and
`val-058-json-bind-list-classptr.cy` are the spec.

---

## Lists, maps, sets

House style: **stack shells, values in the buffer, pipelines that
return values.**

```c
#include "list.h"
#include "map.h"
#include "set.h"

auto nums = List<int> ();
for (int i = 1; i <= 6; i++) nums.Add (i);

auto evens  = nums.Where ((int x) => x % 2 == 0);
auto doubled = evens.Map ((int x) => x * 2);
auto top3   = nums.Take (3);
int  found  = nums.Find ((int x) => x == 4);   // miss → 0 for scalars

auto tags = Set<String> ();
tags.Add ("c"); tags.Add ("c"); tags.Add ("rust");
printf ("%d\n", tags.Count ());                // 2 — String hashes by content

auto ages = Map<String, int> ();
ages["Ada"] = 36;
if (ages.Contains ("Ada"))
    ages["Ada"] = ages["Ada"] + 1;
```

`list[i]` and `map[k]` are real lvalues when `GetMut` exists, so
`fleet[0].Boost(5)` mutates the buffer. `fleet.Get(0).Boost(5)` mutates
a copy and throws the work away.

`Map.Get(k)` **throws** `KeyException` on a miss. Use `GetOr(k, fallback)`
or `TryGet` when absence is normal.

Bare assignment of `List` / `Map` / `Set` is rejected (they are
move-only). Transfer with `move`, or bind a by-value return.

A C array can become a list: `names.ToList()`, or
`new List<String>(names)`. The compiler threads the length next to the
decayed `T*` so a constructor can ask `items.count()`.

Going the other way: `ToArray`, `CopyTo`, `ToJsonArray` / `ToJson`,
`FromJson`. `Map<String,V>` has `ToDict` / `ToJson` and
`Keys()` / `Values()` as value `List`s.

### Lambdas

| Form | What it becomes | Where |
|------|-----------------|--------|
| Non-capturing | a `static` function, thin `T(*)(…)` | anywhere a function pointer is legal |
| Capturing | open-coded at the **call site** | **direct** argument of a listed HOF only |

```c
int is_even (int x) { return (x & 1) == 0; }
auto a = nums.Where (is_even);
auto b = nums.Where ((int x) => (x & 1) == 0);   // same path

int thr = 3;
auto big = nums.Where ((int x) => x > thr);      // captures thr

int flip = -1;
nums.Sort ((int a, int b) => flip * (a - b));

auto pred = (int x) => x > thr;   // error — not a HOF argument
```

HOFs that accept a capturing literal today:

- `List`: `Where` / `Filter` / `Map` / `ForEach` / `Any` / `All` /
  `Find` / `Sort` / `Select`
- `Map`: `Where` / `ForEach` / `Any` / `All`
- `Set`: `Filter` / `ForEach` / `Any` / `All`

No `[=]` / `[&]`, no stored capturing callbacks. That last one needs
fat closures; collection pipelines do not. Design:
[`DOC/LAMBDA-CAPTURE.md`](DOC/LAMBDA-CAPTURE.md).

### When you actually want pointers

```c
auto library = List<Track*> ();
library.owns ();
library.Add (new Track ("Kashmir", "Led Zeppelin", 508));
// ~library deletes every Track

auto epics = library.Where ((Track *t) => t.seconds > 360);
// view — does not steal .owns(), does not free the Tracks
```

One owner per heap object. `Where` / `Copy` / `Take` never copy
`.owns()`. `Map` has `.ownsValues()`, `.ownsKeys()`, and `.owns()`.

---

## Classes, `new`, `defer`

```c
class Point {
    int x, y;

    Point (int x, int y) { this.x = x; this.y = y; }
    ~Point () { printf ("~Point(%d,%d)\n", x, y); }

    Point *withX (int v) { x = v; return this; }
    int sum () { return x + y; }
};

Point *p = new Point (3, 4).withX (10);
defer delete p;                 // LIFO, also on return / break / continue

Point q = Point (1, 2);         // stack: ~Point at scope exit
```

`new T(args) { .field = value, ... }` runs the constructor, then the
designators. The leading `.` is what separates that from collection
brace-init (`new List<int>{1, 2, 3}`, which calls `Add`).

`defer` is the cleanup you write. Heap **Strings** are not your
problem; objects you `new` are, unless you mark them `owned`.

---

## `auto` and `for-in`

```c
auto x   = 42;                              // int
auto d   = { "name": "Ada", "age": 36 };    // dict
auto arr = { 1, 2, 3 };                     // int[3]
```

`for (auto x in …)` walks arrays, `dict`, `List`, `Set`, `Map`, and
string arrays. Two-var form: `for (auto k, v in m)` for `dict` and
`Map`. You can name the type and the element is coerced:

```c
for (String s in d.tags) printf ("%s\n", s);
for (int n in d.xs)      sum += n;
```

On a dict *object*, a single `auto` variable is the **key**. Use two
variables when you want the value too.

---

## Interfaces

```c
interface Drawable { void draw (); }
class Circle impl Drawable { /* ... */ };

Any<Drawable> d = any<Drawable> (new Circle ());
```

There is no `extends` / `virtual`. Structural `interface` + `impl` +
`Any<I>` is the dispatch story.

---

## Exceptions

On by default. A null deref, a divide by zero, or an OOB slice becomes
a catchable exception rather than a mysterious JIT crash.

```c
try {
    int *p = 0;
    int v = *p;
} catch (NullException e) {
    printf ("caught: %s\n", e.msg);
}

throw (OutOfBoundsException, "bad index");
```

Built-in (no include): `Exception`, `NullException`,
`OutOfBoundsException`, `ArithmeticException`, `RuntimeException`,
`KeyException` (8), `TypeException` (7). User IDs start at 100:

```c
enum { MyKeyError = 100 };
throw (MyKeyError, "key missing");
```

Uncaught exceptions print and **`exit(1)`**. `CY_EXC_ABORT=1` if you
want a core. `-fno-exceptions` turns the whole mechanism off.

---

## Fibers

Opt-in: `-ffibers`. Without it, `go` and `await` are ordinary
identifiers.

```c
#include "chan.h"

void worker (Chan<int> *ch) {
    for (int i = 0; i < 100; i++) ch->send (i);
    ch->close ();
}

int main (void) {
    auto ch = new Chan<int> (16);     // new Chan<int>() is rendezvous
    go worker (ch);
    int sum = 0, v = 0;
    while (ch->recv (&v)) sum += v;   // false after close+drain
    add_scheduler (1);                // run the scheduler on this thread
    delete ch;
    return sum == 4950 ? 0 : 1;
}
```

`go f(args)` takes a **plain function**, arguments packed by value
(at most 8, integer or pointer). `await;` yields. `add_scheduler(n)`
with `n > 1` starts `n` pthread workers. `send` after close throws;
so does a second `close`.

```bash
./bin/classyc -I include -ffibers examples/classy-go-chan.cy -eg
```

More: [`DOC/FIBERS.md`](DOC/FIBERS.md), [`DOC/TLS-IMPLEMENTATION.md`](DOC/TLS-IMPLEMENTATION.md),
[`DOC/CLASSY-ATOMICS.md`](DOC/CLASSY-ATOMICS.md).

---

## HTTP, SQLite, a server

```c
#include "httpclient.h"

auto resp = Http.get ("https://pokeapi.co/api/v2/pokemon/ditto");
defer delete resp;
if (resp->ok ()) {
    dict d = resp->asDict ();
    printf ("#%d %s\n", (int)d.id, (char *)d.name);
}
```

OpenSSL is loaded on demand; you do not link it. Full tour:
`examples/classy-fetch.cy`.

`include/sqlite.h` is a small wrapper: `Sqlite.open`, bound
`execute` / `query` → `List<dict>*`, `(User) row` binding,
transactions. Needs `-l sqlite3`. See `examples/classy-restful.cy`
and `examples/classy-querybuilder.cy`.

`include/httpserve.h` is a gunicorn-shaped request/response server.
`examples/http-serve.c` and `examples/classy-http-app.c` link as one
program.

---

## Memory

Four layers, all optional except the String arena (which is just
there):

**Strings.** Every heap `String` — from `+`, methods, `json()`,
`join`, or a helper that returns `String` — is tracked. The compiler
checkpoints at the start of an allocating function and at the top of
each loop iteration, and releases at the bottom / on `continue` /
on `break`. A `return`ed `String` is kept for the caller. If you
assign a tracked string to a variable declared *outside* the loop,
per-iteration release is disabled for that loop (the function still
cleans up on return).

**Collections.** `auto xs = List<T>()` is RAII. Transforms return
values. Use `new` / `owned auto` when a pointer must escape.

**`owned` / `move` / `readonly`.** Opt a *single-owner* heap object
into the static checker. `owned auto x = new Box(1);` is released
exactly once at the end of `x`'s scope. `move x` transfers ownership
and leaves `x` as a read-only view. `readonly y` borrows without
owning. Use-after-move and `delete` of a view are compile errors.

**`detach` / `unowned` / `attach`.** `detach expr` takes a `String`
or class pointer off the current arena ledger and yields the same
value — the explicit escape hatch. `unowned` on a declaration opts
that binding out of ownership tracking entirely. `attach` parses and
type-checks; it does not emit a runtime call yet.

Between check and gen, a CFG ownership pass warns about leaks and
errors on use-after-free / double-free for `malloc`/`new` bindings.
Useful flags: `-fauto-release` (insert `defer free` for definite
leaks), `-fownership-report`, `-fcheck-whole-allocs` (several `.cy`
files as one TU). `-fno-ownership` turns the analyzer off.

The long form lives in [`DOC/readme-ownership.md`](DOC/readme-ownership.md)
and [`DOC/BY-VALUE.md`](DOC/BY-VALUE.md).

---

## AOT and debug info

```bash
./bin/classyc -I include -c -g examples/readme-taste.cy -o /tmp/taste.bmir
./bin/b2obj --dwarf4 /tmp/taste.bmir /tmp/taste.o
cc -g -o /tmp/taste /tmp/taste.o
gdb /tmp/taste
```

Or `./classyc-aot.sh -I include -g examples/readme-taste.cy -o /tmp/taste`.
Line information is DWARF v4.

### jitrunner

Hot-reload and DAP (Zed / VS Code) sit in `bin/jitrunner`:

```bash
bash src/jitrunner/build.sh

./bin/classyc -I include -c -g -o /tmp/prog.bmir examples/readme-taste.cy
./bin/jitrunner /tmp/prog.bmir
./bin/jitrunner /tmp/prog.bmir --watch
./bin/jitrunner --compile examples/readme-taste.cy --watch

./bin/jitrunner --dap-stdio /tmp/prog.bmir --mode interp
./bin/jitrunner --dap 4711
```

`--watch` watches a `.bmir` or, with `--compile`, the source. It does
not take a directory plus `--dap` on the same line. Details:
[`src/jitrunner/JITRUNNER.md`](src/jitrunner/JITRUNNER.md).

`bin/classyc-lsp` is the language server.

---

## Examples and tests

Curated, all `.cy` unless noted:

| File | What it is |
|------|------------|
| `examples/readme-taste.cy` | the crate at the top of this page |
| `examples/classy-aurora-ops.cy` | by-value `List<Ship>` / `Map`, GetMut, GroupBy |
| `examples/classy-neon-grid.cy` | another value-first pipeline |
| `examples/classy-map.cy` | `Map<K,V>` |
| `examples/classy-lambda.cy` | thin lambdas + capturing HOFs |
| `examples/classy-docsearch.cy` | TUI search; capturing `Where` |
| `examples/classy-fetch.cy` | HTTPS + `dict` |
| `examples/classy-go-chan.cy` | fibers (`-ffibers`) |
| `examples/classy-restful.cy` | SQLite REST (`-l sqlite3`) |
| `examples/classy-querybuilder.cy` | `QueryBuilder<T>` |
| `examples/classy-exceptions.cy` | try / catch / throw |
| `examples/classy-safety.cy` | default-on traps |

The rest of `examples/` is fair game. From the repo root:

```bash
./examples/run-examples.sh
```

Behavior claimed here is supposed to stay true in
**[`cy-validate/`](cy-validate/)** (60 programs):

```bash
sh cy-validate/run-validate.sh
```

Regressions: `sh bugs/run-bugs.sh`. Rough edges:
[`cy-validate/SHORTCOMINGS.md`](cy-validate/SHORTCOMINGS.md).

Deeper notes, all under [`DOC/`](DOC/):
[`BY-VALUE.md`](DOC/BY-VALUE.md),
[`GENERICSMEM.md`](DOC/GENERICSMEM.md),
[`LAMBDA-CAPTURE.md`](DOC/LAMBDA-CAPTURE.md),
[`FIBERS.md`](DOC/FIBERS.md),
[`GEN-OPT.md`](DOC/GEN-OPT.md).

---

## How it is put together

```
pre → parse → check → ownership → midopt → gen → MIR
                                              ↘ JIT  (-eg / -el / -eb / -ei)
                                              ↘ .bmir → b2obj → native
```

The parser is hand-written and stays close to the C11 grammar. Check
does types, monomorphization, and most const folding. Ownership is the
leak/UAF lattice. Midopt prunes dead class methods and proves some
safety elisions before any MIR exists. MIR does the SSA work
(GVN/CCP, DCE, LICM, …).

String and dict helpers are small C runtimes imported during
generation. `List` / `Map` / `Set` are headers that the compiler
monomorphizes.

---

## Honest limits

- No `extends` / `super` / `virtual`. Use `interface` + `Any<I>`.
- Capturing lambdas cannot be stored or returned.
- Generic functions infer type arguments (`Max(3, 5)`).
  `Max<int>(3, 5)` and signatures like `List<T> Sort<T>(List<T>)` are
  not there yet.
- `dict` cannot be assigned an array literal.
- JSON bind does not yet fill `Map<K,V>*` fields.
- `attach` is a reserved statement with no runtime effect.
- Exception names exist only at compile time; the runtime stores
  integer ids.
- This is a working compiler with a validation suite, not a promise
  that every C corner or every collection edge is done. Read
  `SHORTCOMINGS.md` before betting a service on a dark corner.

Contributions, bug reports, and slightly-too-ambitious programs are
welcome.

*Built on MIR. Original c2mir by Vladimir Makarov.*
