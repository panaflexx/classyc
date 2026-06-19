# ClassyC — Modern C with Classes, Strings, Dicts & More

> **Note:** ClassyC now includes an LSP server and the `jitrunner` (hot-reload + DAP debug adapter) for a full development experience.

**ClassyC** is a C11 compiler with a carefully chosen set of modern language extensions that make systems programming feel dramatically more productive, while staying true to C's spirit.  Classy is a heavily-modified c2m compiler from MIR.

It is built on the battle-tested [MIR](https://github.com/vnmakarov/mir) JIT/AOT infrastructure, giving you:
- Fast JIT execution (interpreter, lazy codegen, basic-block versioning)
- Real ahead-of-time compilation to native ELF object files (`b2obj`)
- The ability to ship standalone binaries or embed the compiler as a library

## Standout Features

### First-Class `String` (UTF-8)
```c
String greeting = "Hello, 世界 😊";
String name = "Ada" + greeting;;
printf("%s\n", greeting + " from " + name);   // concatenation with auto-promotion

String s = "  Schöne Grüße  ";
s = s.trim().upper();                         // many built-in methods
size_t len = s.length();

String path = "/home/user/docs/report.pdf";
if (path.find(".pdf")) path = path.replace(".pdf", ".txt");
```

### Heterogeneous `dict` (JSON-like)
```c
dict cfg = {
    "server": { "host": "localhost", "port": 8080 },
    "debug": 1,
    "timeout": 30.5
};

printf("%s\n", cfg.server.host);              // dot access
printf("%s\n", cfg["timeut"]);                // array access
cfg.retries = 5;                              // dynamic key creation

for (auto k in cfg) {
    printf("%s = %s\n", k, json(cfg[k]));
}

class Fruit {
    /* Unified declarative definition: one dict, one source of truth.
       Keys are the variant names.
       Each value is a sub-dict containing the int "value" (for switch/case)
       and any other metadata you want (desc, color, season, ...).
    */
    dict variants = {
        "Apple": {
            "value": 0,
            "desc": "A crisp, classic apple."
        },
        "Banana": {
            "value": 10,
            "desc": "Curved yellow banana."
        },
        "Kiwi": {
            "value": 1,
            "desc": "Furry brown kiwi with bright green inside."
        },
        "Mango": {
            "value": 2,
            "desc": "Sweet tropical mango."
        }
    };
};

printf("\nAll variants (single for-in over the unified dict):\n");
for (auto name, data in Fruit.variants) {
    printf("  %s : value=%d  desc=\"%s\"\n",
           name, (int)data.value, data.desc);
}
```

### Classes with Constructors, Destructors & `new`/`delete`
```c
class Point {
    int x, y;

    Point(int x, int y) { this.x = x; this.y = y; }
    ~Point() { printf("~Point(%d,%d)\n", this.x, this.y); }

    Point* withX(int v) { this.x = v; return this; }
    int sum() { return x + y; }
};

Point* p = new Point(3, 4).withX(10);         // heap + chaining
defer delete p;                               // RAII-style cleanup for heap memory
```

### `defer`, `delete`, and Scoped Resource Management
```c
void process() {
    FILE* f = fopen("data.txt", "r");
    defer fclose(f);

    String arena = String.checkpoint();
    defer String.release_to(arena); // Arena memory managed strings, no need to free
}
```

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
Works over arrays, dicts, and (via methods) strings.

### Generics, `List<T>` & Lambdas
```c
List<int> nums = {1, 2, 3};                            // brace-init
nums = nums.Filter((int x) => x > 1).Map((int x) => x * 2);

List<String> files = {"a.txt", "b.pdf", "c.txt"};   // brace-init + String
files = files.Filter((String f) => f.find(".txt"))
               .Map((String f) => f.replace(".txt", ".bak"));

List<Any<View>*> widgets = { any<View>(new Button()), any<View>(new Text()) };
for (auto v in widgets) v->render();   // heterogeneous via type erasure
```

> **Element types & memory** — collections hold scalars, `String`, and pointers
> (e.g. `List<int>`, `Set<String>`, `List<MyClass*>`) directly. A `class` is a
> reference type, so put classes in by pointer (`List<Track*>`, `Set<Track*>`)
> via `new`. For the full picture — value-vs-pointer storage, the
> `Count()`/`Get(int)`/`Set(int,T)` protocol that powers `for-in` and `coll[i]`,
> how `Set<T>` hashes `String` by content but objects by identity, and the
> current limits on by-value class elements — see
> **[GENERICSMEM.md](GENERICSMEM.md)**.

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

### Interfaces & `Any<I>` Erasure
```c
interface Drawable { void draw(); }
class Circle impl Drawable { ... }

Any<Drawable> d = any<Drawable>(new Circle());  // erased handle
```

### Exceptions (opt-in)
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

Enabled with `-fexceptions` (default off; disable explicitly with `-fno-exceptions`). Built-in values: `NullException`, `OutOfBoundsException`, `RuntimeException`, base `Exception`. No `#include` required,

### Full C11 Base + Useful Extensions
- All standard C11 features (minus atomics/complex/VLA/TLS)
- Statement expressions, labels as values, range cases, binary literals, etc.
- Powerful MIR builtins for JIT specialization (`__builtin_prop_*`, `__builtin_jcall`, overflow helpers)

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
| `classy-auto.c`            | `auto` + dict/array disambiguation |
| `classy-generics.c`        | Generic `List<T>` (30 methods, brace-init `{a,b,c}`) |
| `classy-lambda.c`          | Typed lambdas for map/filter/sort/etc. |
| `test-list-stdlib.c`       | Full stdlib List<T> validation |
| `test-array-to-list.cy`    | Array/slice `.ToList()`, `auto` deduction, `List(T*)` ctor |
| `classy-sets.cy`           | Generic `Set<T>` hash set (content-aware `String` hashing) |
| `classy-sets-myclass.cy`   | Custom `WordBag` class over `Set<T>`: word analytics (sort -u, set-grep, stop-words, Jaccard) |
| `classy-search-engine.cy`  | MapReduce inverted-index search engine over `List<T>` of custom classes |
| `classy-collections-class.cy` | `List<Track*>` + `Set<Track*>` over a custom class (Sort/Filter, set algebra by identity) |
| `classy-dict-arena.c`      | Arena-backed dicts (`new dict(size)`) |
| `test-any-arena.c`         | `Any<I>` type erasure + arena-managed handles |
| `test-interface.c`         | `interface` + `impl` structural conformance |
| `test-any.c`               | Heterogeneous `List<Any<View>*>` (arena + non-arena) |
| `classy-exceptions.cy`     | `try`/`catch`/`throw` (opt-in via `-fexceptions`) |

Run them all with:
```bash
examples/run-examples.sh
```

## Memory Management

ClassyC provides lightweight arena allocators for high-level types:

- **String arena** — `String.checkpoint()` / `String.release_to(arena)` or `defer String.release_to(...)` reclaims all strings allocated since the checkpoint in one shot.
- **Dict arena** — `new dict(bytes)` creates an arena-backed dict; `delete d` frees the entire arena and its contents.
- **List<T> and collections** — The generic `List<T>` (and `any<I>` handles) use the same object-arena model: elements registered in a scope-bound arena are automatically reclaimed on scope exit.

Typical pattern:

```c
String a = String.checkpoint();
defer String.release_to(a);

dict cfg = new dict(256*1024);
defer delete cfg; // Heap allocated must be freed manually

List<String> names = List<String>();
// ... populate and use ...
// all released automatically at scope exit
```

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
user-defined collections), `interface`/`Any<I>` erasure, opt-in exceptions, and
array/slice → `List<T>` conversion with lengths flowing into generics. In-progress
directions include richer container types and broader standard-library coverage.

Contributions, bug reports, and wild ideas are welcome!

---

*Built with ❤️ on top of MIR. Original c2mir design by Vladimir Makarov.*
