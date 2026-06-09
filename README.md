# ClassyC — Modern C with Classes, Strings, Dicts & More

**ClassyC** is a C11 compiler with a carefully chosen set of modern language extensions that make systems programming feel dramatically more productive, while staying true to C's spirit.  Classy is a heavily-modified c2m compiler from MIR.

It is built on the battle-tested [MIR](https://github.com/vnmakarov/mir) JIT/AOT infrastructure, giving you:
- Fast JIT execution (interpreter, lazy codegen, basic-block versioning)
- Real ahead-of-time compilation to native ELF object files (`b2obj`)
- The ability to ship standalone binaries or embed the compiler as a library

## Standout Features

### First-Class `String` (UTF-8)
```c
String greeting = "Hello, 世界 😊";
String name = "Ada";
printf("%s\n", greeting + " from " + name);   // concatenation with auto-promotion

String s = "  Schöne Grüße  ";
s = s.trim().upper();                         // many built-in methods
size_t len = s.length();
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
    /* UNIFIED declarative definition: one dict, one source of truth.
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
    int sum() { return this.x + this.y; }
};

Point* p = new Point(3, 4).withX(10);         // heap + chaining
defer delete p;                               // RAII-style cleanup
```

### `defer`, `delete`, and Scoped Resource Management
```c
void process() {
    FILE* f = fopen("data.txt", "r");
    defer fclose(f);

    String arena = String.checkpoint();
    defer String.release_to(arena);

    // ...
}
```

### f-Strings (Interpolated Strings)
```c
String user = "bob";
int score = 42;
String msg = f"Hello {user}, your score is {score}";
```

### Nice `auto` + Disambiguation
```c
auto x = 42;                    // int
auto d = {"name": "Ada", "age": 36};   // dict
auto arr = {1, 2, 3};           // int[3]
```

### `for (auto x in ...)` Loops
Works over arrays, dicts, and (via methods) strings.

### Full C11 Base + Useful Extensions
- All standard C11 features (minus atomics/complex/VLA/TLS)
- Statement expressions, labels as values, range cases, binary literals, etc.
- Powerful MIR builtins for JIT specialization (`__builtin_prop_*`, `__builtin_jcall`, overflow helpers)

## How to Build

```bash
cd classyc
git submodule update
cmake .             # builds in main dir, 
make                # builds the `classyc` (or `c2m`) compiler
```

The build also produces `b2obj` for ahead-of-time ELF object generation.

## Usage

### JIT Execution (fast iteration)
```bash
classyc example.c -eg          # generate machine code + run
classyc example.c -el          # lazy function generation
classyc example.c -eb          # lazy basic-block generation
```

### Ahead-of-Time Compilation
```bash
classyc -c example.c -o example.bmir  # emit MIR binary
b2obj example.bmir example.o          # produce native ELF .o
```

You can link the resulting `.o` files with any standard C toolchain.

### As a Library
`classyc.c` (the single-file compiler) can be embedded exactly like the original c2mir. See the original c2mir documentation for the library interface.

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

Run them all with:
```bash
examples/run-examples.sh
```

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

Planned / in-progress directions include lambdas (simple delegate style first) and generics (starting with containers like `List<T>`).

Contributions, bug reports, and wild ideas are welcome!

---

*Built with ❤️ on top of MIR. Original c2mir design by Vladimir Makarov.*
