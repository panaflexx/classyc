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
    "timeout": 30.5,
    "logfiles": ["access.log", "error.log"]
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

JSON arrays come through too — parse, then index by position **or** iterate:

```c
dict d = json("{\"items\":[{\"name\":\"ada\",\"score\":42},"
              "            {\"name\":\"cy\", \"score\":99}]}");

// Indexed access
printf("%s\n", (char*)d.items[0].name);       // "ada"
int score = (int)d.items[0].score;            // 42

// Length: works for both array and object dicts
int n = (int)d.items.length();                // 2  (alias: .count())

// for-in dispatches on the runtime tag: object -> (key, value),
// array -> (index, element).  Single-var form counts iterations.
for (auto i, item in d.items)
    printf("%d: %s = %d\n", i, (char*)item.name, (int)item.score);
```

Every dict access is a **tagged box** — `d.items[0].score` returns a `DictValue*`,
not a raw int. That means deep navigation, `json(leaf)` re-serialization, and the
typed JSON binding below are all lossless: a numeric leaf survives a round-trip
through `dict v = d.items[0].score; json(v)` (prints `"42"`).

`dict` also supports arena allocation (`new dict(bytes)`) and is the return type of `HttpResponse::asDict()`.

### Typed JSON Binding: `(T) d` and `(T)? d`

C#-style "JSON-to-struct deserialization" — cast a `dict` directly to a `class`
or plain `struct` and the compiler walks the target's member list, filling each
field from the matching dict key:

```c
class Address { String city; int zip; };
class User    { String name; int age; Address addr; };

dict d = json(req.body);

User u = (User) d;              // strict: throws KeyException on missing field
User u = (User)? d;             // lenient: missing fields default to 0 / NULL
```

* **Strict** (`(T) d`): a missing field throws a catchable `KeyException` with
  `e.msg == "missing field 'F' in T"`.
* **Lenient** (`(T)? d`): missing fields stay at zero/NULL; lenience propagates
  recursively into nested class and struct members.
* Works on **plain C structs** too — `struct Point { int x, y; }; Point p = (struct Point) d;`
  — and freely mixes class and struct nesting (`class Sprite { struct Pixel pixel; }`).
* Scalars, `String`, nested by-value classes / structs, and **collection fields**
  (`List<T>*` from a JSON array) are supported.  The bound object owns the
  heap collection (its destructor must `delete` it, as `List<T>::~List` does);
  `String` elements are private copies, so the source dict can be freed right
  after the bind.  `Set<T>*` works the same way (any class with a default ctor
  + `Add(T)`); `Map<K,V>*` and pointer-to-class elements (`List<User*>*`) are
  Phase 3.
* No annotations needed — the binder works off the class's declared members.
  Field names must match the dict keys verbatim (no case conversion).

See `cy-validate/val-020-json-binding.cy` (scalars/structs) and
`cy-validate/val-024-json-binding-collections.cy` (collection fields) for the
full coverage matrix.

### Typed `Map<K, V>` Hash Maps (`include/map.h`)
The typed, type-safe sibling of `dict`: a generic open-addressing hash map that
fixes its key and value types at compile time, stores values inline (no boxing),
and works with any key type. String keys are hashed by **content**, scalars by
**value**, and objects (pointers) by **identity** — chosen at compile time via
`_Generic`, exactly like `Set<T>`.

```c
#include "map.h"

// Stack Map (preferred) — ~ages at scope exit
auto ages = Map<String, int>();
ages["Ada"] = 36;                       // write → Set(key, val)
ages["Ada"] = ages["Ada"] + 1;          // read  → Get (or GetMut lvalue for classes)
if (ages.Contains("Ada")) { /* ... */ }

for (auto name, age in ages)            // (key, value) — V may be a by-value class
    printf("%s is %d\n", name, age);

// By-value class values (happy path)
auto wing = Map<String, Ship>();
wing["AURORA"] = Ship(1, "AURORA", core, 88, 142);
wing["AURORA"].Boost(5);                // [] → GetMut lvalue, mutates buffer
for (auto callsign, s in wing)
    printf("%s heat=%d\n", callsign, s.heat);

// Pointer values when you need shared identity / .ownsValues()
auto lib = Map<String, Track*>();
lib.ownsValues();
lib["Kashmir"] = new Track("Kashmir", 508);
```

`Map<K, V>` plugs into the same language sugar as `List<T>` / `Set<T>`:
- **Subscript** `m[k]` / `m[k] = v` → `Get`/`Set`; when `GetMut(K)` exists, **read
  `m[k]` is a true lvalue** into the dense value array so `m[k].Method()` mutates
  storage (not a temporary copy).
- **for-in** `for (auto k in m)` / `for (auto k, v in m)` — keys and key/value pairs
  in insertion order (`Count` / `KeyAt` / `ValAt`). **`V` (and `K`) may be by-value
  classes**, same as `List.Get` for-in; loop vars are stack slots each iteration.
  Body locals (e.g. `Ship leader = v.First()`) get their own frame slots after the
  loop vars — nested class values next to for-in `List` shells are safe.
- **Mut helpers:** `m.GetMut(k)` / `m.ValMut(i)` return `V*` when you want an explicit
  pointer (invalidated by rehash).

See `examples/classy-map.cy`, `examples/classy-aurora-ops.cy`, and
`cy-validate/val-044-map-class-forin.cy`.

#### `dict` / JSON conversions
A `Map<String, V>` converts straight to a JSON `dict` or String. `ToDict()`
dispatches the value type `V` automagically (via `nameof<V>()`): scalars → number,
`String` → string, `dict` → passthrough. `ToJson()` serializes that to an
independent String. `Keys()` / `Values()` collect into **value** `List<K>` /
`List<V>` shells (RAII — no `delete`):

```c
auto ages = Map<String, int>();
ages["ada"] = 36;
ages["alan"] = 41;

dict   d = ages.ToDict();   // {"ada":36,"alan":41}
String j = ages.ToJson();   // "{\"ada\":36,\"alan\":41}"

auto   ks = ages.Keys();    // value List<String> — ~ks at scope exit
auto   vs = ages.Values();  // value List<int>
String vj = vs.ToJson();    // "[36,41]"
```

Keys are read as `String`, so these are intended for `Map<String, V>`; for
`Map<String, dict>` serialize `ToDict()` yourself (`ToJson()` would free the
referenced value dicts).

### Generics, `List<T>`, `Set<T>` & Lambdas
**House style:** prefer stack/value shells with RAII. Transforms return
`List`/`Map`/`Set` **by value** so local pipelines need no `owned`/`delete`.
Use `new` / `owned auto` only when a collection must escape as a pointer.

```c
#include "list.h"
#include "set.h"

// Stack List — ~nums frees the buffer at scope exit
auto nums = List<int>();
nums.Add(1); nums.Add(2); nums.Add(3); nums.Add(4); nums.Add(5); nums.Add(6);

// Value-returning LINQ (RAII shells — no owned/delete)
auto evens   = nums.Where((int x) => x % 2 == 0);
auto doubled = evens.Map((int x) => x * 2);
auto top3    = nums.Take(3);                 // chain-friendly
auto silver  = nums.Skip(1).Take(1);
auto by_par  = nums.GroupBy((int x) => x % 2);  // Map<int, List<int>> nested List shells

// By-value class elements (happy path — no *)
auto fleet = List<Ship>();
fleet.Add(Ship(1, "AURORA", core, 88, 142));
fleet[0].Boost(5).Boost(2);                  // [] → GetMut lvalue (mutates buffer)
//  fleet.Get(0).Boost(5);                   // WRONG: Get copies; Boost is lost
auto hot = fleet.Where((Ship s) => s.IsHot());
auto heats = fleet.Select((Ship s) => s.heat);   // Select on pure stack List works
Ship deep = fleet.Find((Ship s) => s.IsDeep());  // miss = zero-init; use s.Alive()
int want = 1;
Ship hit = fleet.Find((Ship s) => s.id == want); // capturing Find
int flip = 1;
fleet.Sort((Ship a, Ship b) => flip * (a.heat - b.heat)); // capturing Sort
int boost = 10;
auto scaled = fleet.Select((Ship s) => s.heat + boost);    // capturing Select

auto files = List<String>();
files.Add("a.txt"); files.Add("b.pdf"); files.Add("c.txt");
auto txt = files.Where((String f) => f.ends_with(".txt"));

// Heap when you need pointer identity / long-lived share
owned auto widgets = new List<Any<View>*>();
widgets.Add(any<View>(new Button()));
widgets.Add(any<View>(new Text()));
for (auto v in widgets) v.render();

// Set — stack value; content-aware for String, identity for objects
auto tags = Set<String>();
tags.Add("c"); tags.Add("c"); tags.Add("rust");
printf("unique tags: %d\n", tags.Count());   // 2
```

> `List<T>` provides `Filter`/`Where`, `Map`, `Select<U>`, `Take`/`Skip`,
> `Copy`/`Slice`/`Plus`/`Distinct`, `ForEach`, `Find`/`FindOr`, and `Sort`.
> Transform methods return **value** shells (C++ `vector`-style). Bare assign of
> `List`/`Map`/`Set` is banned (would double-free); transfer with `move`, or bind
> a by-value return.

> **Element types & memory (product stance):**
> - **Happy path:** scalars, `String`, and **by-value classes** (`List<Ship>`,
>   `List<Signal>`). Prefer this for everyday domain types.
> - **In-place mutation:** `list[i]` / `map[k]` lower to **`GetMut`** when present
>   and yield a true buffer lvalue — `list[i].Method()` mutates storage. Explicit
>   helpers: `GetMut` / `FirstMut` / `LastMut` (List), `GetMut` / `ValMut` (Map).
> - **Advanced / identity:** `List<T*>.owns()` for shared graphs, C interop, or
>   non-copyable resources. Views from `Where`/`Copy`/`Take` never steal `.owns()`.
>
> Full picture: **[GENERICSMEM.md](GENERICSMEM.md)**, **[BY-VALUE.md](BY-VALUE.md)**.
> Showcase: `examples/classy-aurora-ops.cy`. Tests: `cy-validate/val-043-list-getmut.cy`.

#### Lambdas (typed, thin C, capturing HOF args)

ClassyC has **fat-arrow lambdas** with two lowering paths — both stay
C-shaped (no `{fn,env}` fat pointers):

| Form | Lowering | Where it works |
|------|----------|----------------|
| **Non-capturing** | `static` function → thin `T(*)(…)` | Anywhere a function pointer is legal: HOF args, `apply(f)`, assignment |
| **Capturing** | open-coded `for` / `for-in` at the **call site** | **Direct** argument to recognized HOFs only |

```c
// Non-capturing — thin function pointer (stable ABI)
int is_even(int x) { return (x & 1) == 0; }
auto a = nums.Where(is_even);
auto b = nums.Where((int x) => (x & 1) == 0);  // same path

// Capturing — free outer locals stay normal names in the caller frame
int thr = 3;
auto big = nums.Where((int x) => x > thr);

int want = 5;
Ship s = fleet.Find((Ship x) => x.id == want);  // capturing Find

int flip = -1;
nums.Sort((int a, int b) => flip * (a - b));    // capturing Sort comparator

int mul = 10;
auto scaled = nums.Select<int>((int x) => x * mul);  // capturing Select

int sum = 0;
nums.ForEach((int x) => { sum += x; });       // may mutate outer locals

String prefix = "err";
auto bad = logs.Where((String s) => s.starts_with(prefix));

int floor = 50;
auto hot = board.Where((String k, int v) => v >= floor);  // Map
```

**HOFs that accept capturing literals (v1):**

| Receiver | Methods |
|----------|---------|
| `List<T>` | `Where` / `Filter` / `Map` / `ForEach` / `Any` / `All` / **`Find`** / **`Sort`** / **`Select`** |
| `Map<K,V>` | `Where` / `ForEach` / `Any` / `All` |
| `Set<T>` | `Filter` / `ForEach` / `Any` / `All` |

```c
// Chains work left-to-right (each capturing HOF open-coded on its own)
auto q = nums.Where((int x) => x > thr).Take(10);

// GroupBy — Map<int, List<int>> (nested List shells, RAII)
auto by = nums.GroupBy((int x) => x % 2);
for (auto k, bucket in by)
    printf("%d: %d\n", k, bucket.Count());
```

**Rules (v1):**

- Capture is **only** for a lambda that is the **direct** argument of a HOF above
  (`xs.Where((int x) => x > thr)` ✅).
- Non-capturing lambdas keep working as ordinary C function pointers.
- Assigning / returning a **capturing** lambda is a hard error with a hint:

```c
int thr = 5;
auto pred = (int x) => x > thr;   // ERROR — not a HOF argument
// hint: use xs.Where((int x) => x > thr) inline, or a non-capturing function
```

- No `[=]` / `[&]` syntax; free vars resolve lexically like nested blocks.
- Predicate / project / compare HOFs that capture should use an **expression body**
  or a single `return expr;` (multi-return blocks deferred). `Sort` needs a two-param
  comparator `(T a, T b) => …`.
- **Not** for stored UI callbacks / `std::function`-style escape (`button.callback([this]{…})`).
  That needs fat closures later; collection pipelines do not.

Details & design: **[LAMBDA-CAPTURE.md](LAMBDA-CAPTURE.md)**.  Tests:
`cy-validate/val-042-lambda-capture.cy` (Find / Sort / Select capture),
`examples/classy-lambda.cy`, `examples/classy-docsearch.cy`
(`search_docs` min-score filter).

#### Ownership: by-value first; `.owns()` for pointer graphs

**Default for app code:** store **values** in the list/map buffer. No `new`, no
`.owns()`, no stars in lambdas.

```c
auto fleet = List<Ship>();
fleet.Add(Ship(1, "AURORA", core, 88, 142));
fleet[0].Boost(5);                            // mutates buffer via GetMut
auto deep = fleet.Find((Ship s) => s.IsDeep());
```

**When you need identity / shared graphs / C interop:** use pointers and mark
ownership so `~List` / `delete` frees pointees.

```c
// OWNING stack list of pointers — pointees deleted in ~List
auto library = List<Track*>();
library.owns();
library.Add(new Track("Kashmir", 508));
library.Add(new Track("Africa", 295));
// ~library at scope exit frees every Track ✅

// NON-OWNING value view (Where never copies .owns())
auto epics = library.Where((Track* t) => t.seconds > 360);
// ~epics frees only its buffer; Tracks still owned by library

auto byId = Map<int, Track*>();
byId.ownsValues();
```

Rules of thumb:
- **Shell first:** stack `auto xs = List<T>();` for locals; `owned`/`new` to escape.
- **Values by default** (`List<Ship>`). **Pointers** for shared identity or FFI.
- **Exactly one owner per object** for `T*`. Views from `Where`/`Copy`/`Take`
  never steal `.owns()`.
- **By-value elements** already destroy via `__destroy` (no `.owns()`).
- Prefer **quiet** destructors on types stored by value (side-effect `printf`/`free`
  in `~T` multiplies under `Copy`/`Where`).

See `include/list.h`, `set.h`, and `map.h`.

#### Generic Functions & Methods

Generic *functions* (free functions, not class methods) let you write one
definition that is monomorphized for each distinct inferred type-argument set —
the foundation for `sort`, `map`, `reduce`, `hash`, and equality utilities:

```c
T Max<T>(T a, T b) { return a > b ? a : b; }

auto m = Max(3, 5);              // T=int inferred -> __genfn_Max_int
auto d = Max(1.5, 2.5);          // T=double inferred -> __genfn_Max_double

// Multi-parameter generics infer each parameter from the matching argument:
K First<K, V>(K k, V v) { return k; }
auto f = First(1, "hello");      // K=int, V=String
```

At a call site, the compiler infers the type arguments from the call's argument
types (the first parameter whose declared type is exactly `T` fixes `T`),
deep-copies the template with `T` substituted, renames it to a mangled
specialization (`__genfn_<Name>_<args>`), and injects it into the module so it
is checked and code-generated like any other function. Repeated calls with the
same inferred types reuse the cached specialization.

The template itself is skipped during checking and codegen (only its
monomorphized specializations are real functions), mirroring how generic *class*
templates work.

#### Nested generics: `List<T*>` inside another generic

A generic class body may instantiate another generic with its *own* type
parameter — including a **pointer** to it, like `List<T*>`. The reference is
deferred as a placeholder while the outer template is parsed and only
materialized (with the pointer level preserved) once the outer class is
specialized with a concrete type:

```c
#include "list.h"

class Repository<T> {
    List<T*>* items;

    Repository() {
        // `T*` flows through to List's element type: Repository<User>
        // materializes an owning List<User*>.
        this->items = new List<T*>().owns();
    }
    ~Repository() { delete items; }        // frees the list AND every T

    void add(T* e) { this->items->Add(e); }
    T*   get(int i) { return this->items->Get(i); }
    int  count()   { return this->items->Count(); }
};

auto users = new Repository<User>();
defer delete users;                        // frees the repo, its list, and users
users->add(new User("Ada"));
printf("%s\n", (char*)users->get(0)->name);
```

This is exactly what powers the SQLite `QueryBuilder<T>` (see
`examples/classy-querybuilder.cy`), whose `ToList()` returns an owning
`List<T*>` of typed entities. Multi-level (`T**`) and multi-parameter
(`Map<K, V*>`) forms work the same way.

#### Compile-time reflection: `nameof` / `typeof`

Generic bodies (and free code) can stringify types and values:

```c
enum Fruit { apple = 1, banana = 2, cherry = 3 };

// Type-level (specializes inside generics; nameof strips *, typeof keeps them):
const char* a = nameof<int*>();     // "int"
const char* b = typeof<int*>();     // "int*"
const char* c = nameof<Fruit>();    // "Fruit"

// Value-level method forms:
printf("enum=%s\n", apple.nameof());   // "apple"
printf("type=%s\n", apple.typeof());   // "Fruit"

enum Fruit f = cherry;
printf("%s\n", f.nameof());            // "cherry" (runtime reverse map)

int x = 42;
printf("%s\n", x.nameof());            // "x"  (C#-style identifier spelling)
printf("%s\n", x.typeof());            // "int"

// Free form for identifiers:
printf("%s\n", nameof(banana));        // "banana"
```

`List.ToJsonArray` / `Map.ToDict` use `nameof<T>()` / `nameof<V>()` for their
type-dispatch branches. See `cy-validate/val-030-nameof-typeof.cy`.

#### Generic methods: `Select<U>`

Class methods may declare their own type parameters (in addition to the class's).
`List<T>.Select` projects into a different element type and returns a **value**
`List<U>` (RAII). Works on pure stack receivers as well as heap `List*`:

```c
int times2(int x) { return x * 2; }
String nameOf(User* u) { return u->name; }

// Stack (preferred locals)
auto xs = List<int>();
xs.Add(1); xs.Add(2); xs.Add(3);
auto d = xs.Select<int>(times2);            // explicit U — value List
auto e = xs.Select(times2);                 // U inferred from fn return type

// Heap still works
List<int>* hp = new List<int>{1, 2, 3};
auto d2 = hp->Select<int>(times2);
defer delete hp;

auto fleet = List<Ship>();
// ...
auto heats = fleet.Select((Ship s) => s.heat);  // stack + by-value T
```

See `cy-validate/val-031-generic-methods.cy` (heap) and
`cy-validate/val-046-select-stack.cy` (pure stack). `GroupBy` returns a value
`Map<G, List<V>>` with nested List shells (val-049, val-050).

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

### `List<T>` → Arrays & `dict` Conversions
`include/list.h` ships C#/LINQ-flavored converters that go the other way — from a
`List<T>` back to a raw array or a JSON `dict`:

```c
#include "list.h"

List<int>* nums = new List<int>{ 10, 20, 30 };

// — to a base array —
int* arr = nums->ToArray();   // fresh heap T[] (bulk memcpy); caller frees
int  buf[3];
nums->CopyTo(buf);            // bulk-copy into a caller-provided buffer

// — to a dict / JSON —
dict out = { "items": nums->ToJsonArray() };   // {"items":[10,20,30]}
```

`ToJsonArray()` is **automagical**: it inspects the element type at compile time
(via `nameof<T>()`) and emits the matching JSON value for every element —
`int`/`long`/`short` → number, `double`/`float` → number, `String` → string,
`dict` → passthrough. One call converts a `List<int>`, `List<double>`, or
`List<String>` with no per-type helper:

```c
List<double>* ds = new List<double>{ 1.5, 2.5, 3.0 };
List<String>* ws = "a,b,c".split(",");

dict payload = {
    "nums":    ds->ToJsonArray(),   // [1.5,2.5,3]
    "words":   ws->ToJsonArray(),   // ["a","b","c"]
};
```

For full control there are projection-based converters (the `Select` / `ToDictionary`
of JSON building), plus the named shortcuts:

```c
// project each element to a dict value
dict arr = library->ToJsonArrayBy((Track* t) => dict_create_string(t->title));

// build a JSON object keyed by a selector (LINQ ToDictionary)
dict byId = library->ToDictBy(
    (Track* t) => (char*)t->title,          // key selector
    (Track* t) => dict_create_int64(t->id)  // value selector
);

List<String>* parts = csv.split(",");
dict items = { "items": parts->StringsToJsonArray() };  // List<String> shortcut
dict ids   = { "ids":   nums->IntsToJsonArray() };      // List<int>    shortcut
```

`ToDict()` converts a `List<dict>` straight to a JSON array (e.g. SQL rows from
`db->query(...)` → a JSON response body).

#### Round-trip: `FromJson(dict)` and `ToJson()`
The converters also run in reverse. `List<T>.FromJson(dict array)` is a static
factory that rebuilds a typed list from a JSON array — the **automagical reverse**
of `ToJsonArray()`. The dict→scalar coercion unwraps each element to `T`, so one
call covers `List<int>`, `List<double>`, `List<String>`, and `List<dict>`
(passthrough) with no per-type helper. The caller owns the result:

```c
dict d = json("{\"xs\":[10,20,30],\"tags\":[\"a\",\"bb\",\"ccc\"]}");

List<int>*    xs   = List<int>.FromJson(d.xs);     // [10, 20, 30]
List<String>* tags = List<String>.FromJson(d.tags); // ["a", "bb", "ccc"]
defer delete xs;
defer delete tags;
```

`ToJson()` serializes a scalar list (`int`/`double`/`String`) straight to a JSON
String — it builds a transient dict via `ToJsonArray()`, serializes it, and frees
it, so the returned String is independent:

```c
String a = xs->ToJson();    // "[10,20,30]"
String b = tags->ToJson();  // ["a","bb","ccc"]
```

> For a `List<dict>` use `ToDict()` and serialize the owning dict yourself —
> `ToJson()` would free the referenced element dicts.

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

#### Object initializers: `new T(args) { .field = value, ... }`
A `new` expression can be followed by a C/C++23-style **object initializer** —
a brace block of `.field = value` designators that run as post-construction
stores on the freshly-allocated object. The constructor (if any) runs first;
the designators then fill named fields, so you skip a wall of `p->field = ...`
assignments:

```c
Patient* p = new Patient() {        // zero-arg ctor, then field stores
    .firstName = "Ada",
    .lastName  = "Lovelace",
    .age       = 36,
};

Patient* q = new Patient(startId) { // ctor arg sets id, designators do the rest
    .firstName = "Alan",
    .home      = (Point){ .x = 3, .y = 4 },   // by-value aggregate field
};
```

Each store reuses normal field-assignment semantics: value-semantic `String`
ownership (the object frees them on `delete`), scalar coercion, and by-value
aggregate copy. Unknown fields and type-incompatible values are compile errors.
The leading `.` distinguishes this from the collection brace-init
`new List<int>{1, 2, 3}` (which calls `Add` per element).

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

### Managed Ownership: `owned`, `move`, `readonly`

On top of unmanaged C11 and plain `new`/`delete`, ClassyC offers an **opt-in**,
GC-like layer for single-owner heap objects. You mark a binding `owned` and the
compiler guarantees it is released **exactly once** — no `defer delete`, no
manual cleanup, and no double frees — by statically tracking where ownership
lives at every point in the function. Nothing here is on by default: ordinary
pointers, `new`/`delete`, and the arena keywords above keep working unchanged.

| Keyword | Position | What it does |
|---|---|---|
| `owned` | declaration prefix | opt a binding into the managed, single-owner, move-only lifetime |
| `move` | expression | transfer ownership out of a binding; the source becomes a read-only view |
| `readonly` | expression | borrow a non-owning read-only view of an owned object |

```c
class Box {
    int v;
    Box(int v) { this.v = v; }
    ~Box() { /* freed automatically — you never call delete */ }
};

void demo() {
    owned auto x = new Box(1);   // x is the single owner
    auto y = move x;             // ownership x -> y; x is now a read-only view
    auto z = readonly y;         // z borrows a non-owning view of y

    printf("%d %d %d\n", x->v, y->v, z->v);  // reads through all three are fine
}   // <- compiler releases `y` here (runs ~Box once); x and z are never freed
```

#### How `owned` cleans up and deletes

Between the type checker and code generator, the static ownership pass
(`src/ownership.c`) follows each managed binding through a small ownership
lattice (Owned → moved/escaped → released). At every scope exit it knows which
binding currently *owns* the object, and it synthesizes a `delete <owner>;`
for it. That synthesized release is routed through the same `defer` machinery
that backs explicit `defer delete`, so it unwinds at the end of the block
**and** on every `return` / `break` / `continue` path — running the
destructor (`~Box`) and freeing the object exactly once.

- **No keyword needed at the call site.** `owned` is the whole contract; the
  cleanup is invisible in the source but real in the generated code. Releasing
  happens at the end of the *owning binding's* scope — including a nested
  `{ ... }` block, not just the function body.
- **Move means the new owner cleans up.** Once ownership is `move`d out of a
  binding, that binding is no longer the owner, so it is **not** freed — only
  the binding that holds ownership at scope exit is. This is how single
  ownership avoids double frees across `auto y = move x;` and longer chains
  (`x -> y -> w` frees once, via `w`).
- **`move`-initialized bindings are managed too.** `auto y = move x;` makes
  `y` the new managed owner even though it is declared with a plain `auto`;
  ownership flowing in via `move` promotes the receiver automatically.
- **`unowned` is still the opt-out.** Prefix a declaration with `unowned` to
  take manual responsibility and suppress all managed cleanup for it.

#### How `readonly` works

`readonly <expr>` yields the *same* pointer value as its operand but confers
**no ownership**: a read-only view never releases the object and is never
counted as an owner. A view can be held anywhere — a local, a global, or an
object field — with the single rule that it must not be used after its owner
is gone. Because views don't own, creating one has no effect on when (or
whether) the underlying object is freed:

```c
owned auto cfg = new Config();
auto v = readonly cfg;     // borrow; cfg still owns and will be released once
use(v->host);              // reading through the view is fine
```

A binding left behind by `move` is itself a read-only view of the moved-from
object — you may still *read* through it, but you may no longer treat it as an
owner.

#### What the compiler enforces

The ownership pass turns single-owner violations into compile-time
diagnostics:

- **Use-after-move** — `move`ing or otherwise consuming a binding whose
  ownership already moved out is an `error` (the binding is now just a view).
- **`delete` of a moved-from view** — an `error`: the new owner is responsible
  for the object, not this view.
- **Redundant `delete` of an `owned` binding** — a `warning`: the compiler
  already releases it at scope exit, so an explicit `delete` is unnecessary
  (and would risk a double free).

Soft-keyword notes: `move` and `readonly` are **expression-leading** soft
keywords — like `detach`/`new`, they only shadow an identifier when they start
an expression, so `a + move` reads the variable `move` while `move x`
transfers ownership. `owned` is a **declaration-prefix** soft keyword (like
`unowned`), so it stays freely usable as an identifier in expressions.

See `examples/test-owned-move-readonly.cy` for a runnable demo,
`examples/test-owned-errors.cy` for the rejected cases, and
`cy-validate/val-022-owned-move-readonly.cy` for the executable spec.

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
Works over arrays, `dict`, `List<T>`, `Set<T>`, `Map<K,V>`, and (via methods) strings. Keyed variant `for (auto k, v in m)` is supported for `dict` and `Map`. For a `dict` carrying a JSON **array**, the two-var form binds `(index, element)` (runtime-tag dispatched) so the same loop walks both objects and arrays without a type switch.

Body declarations are ordinary nested scopes relative to the for-in loop vars, so patterns like GroupBy + in-loop locals are fine:

```c
auto by = roster.GroupBy((Ship s) => s.SectorKey());
for (auto k, v in by) {
    Ship leader = v.First();          // own stack slot — does not clobber v
    printf("%d leader=%s\n", k, leader.callsign);
    v.ForEach(print_banner);          // still a live List shell
}
```

**Typed loop variables.** Instead of `auto`, name the element type and the loop binds the element *coerced to that type* — handy for JSON arrays, where the element would otherwise be a tagged `dict` you have to cast:

```c
dict d = json("{\"tags\":[\"fever\",\"cough\"], \"xs\":[10,20,30]}");

for (String s in d.tags) printf("%s\n", s);   // each element unwrapped to String
for (int n in d.xs)      sum += n;             // each element unwrapped to int

for (int v in carray)    total += v;          // also works for C arrays / List<T>
for (String k, auto v in obj) { /* k = key */ }  // two-var typed form
```

The typed form desugars to the two-var loop internally, so the element keeps its natural type before being coerced. (For a plain `auto` single-var loop over a dict *object*, the variable is the key, as before; use the two-var form for keys + values.)

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

### Go-style Fibers & Channels (`-ffibers`, opt-in)
Cooperative, stackful fibers (minicoro) and typed channels (cchan), Go flavor.
Fully opt-in: without `-ffibers` nothing changes — `go`/`await` stay ordinary
identifiers and no fiber runtime is referenced.

```c
#include "chan.h"          // Chan<T> + cyfiber runtime decls

void worker(Chan<int> *ch) {
    for (int i = 0; i < 100; i++) ch->send(i);   // parks if full
    ch->close();
}

int main(void) {
    auto ch = new Chan<int>(16);   // buffered; new Chan<int>() = rendezvous
    go worker(ch);                 // spawn fiber (args value-packed)
    int sum = 0, v = 0;
    while (ch->recv(&v)) sum += v; // false after close+drain (Go ok-idiom)
    add_scheduler(1);              // explicit runtime: init + run
    delete ch;
    return sum == 4950 ? 0 : 1;
}
```

- `go f(args);` — direct plain-function call; args captured **by value**
  (max 8, integer/pointer types only).
- `await;` / `await expr;` — pure cooperative yield; channel parking is
  explicit inside `Chan<T>` ops (`try` + yield, never OS-thread blocking).
- `add_scheduler(n)` — `n<=1`: single-thread scheduler on the caller;
  `n>1`: pool of `n` pthread workers, fibers pinned to workers.
- `Chan<T>`: `send` **throws** on send-after-close, `close` throws on double
  close; `try_send/try_recv`, `send/recv_timeout`, `len/cap/closed`.

Run: `./bin/classyc -I include -ffibers examples/classy-go-chan.cy -eg`
AOT: `./classyc-aot.sh -I include -ffibers examples/classy-go-chan.cy -o prog`
Design doc + roadmap (TLS, select, work stealing): `FIBERS.md`.

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
| `classy-lambda.cy`         | Typed lambdas: thin fn ptrs + capturing HOF open-code |
| `classy-docsearch.cy`      | Doc search TUI — capturing `Where` with local `min_score` (no `g_*`) |
| `classy-neon-grid.cy`      | **By-value house style:** stack List/Map, value Where/Take/Skip, Pilot*.owns, LapSample DTO |
| `classy-aurora-ops.cy`     | **By-value showcase:** `List<Ship>` happy path, GetMut, value LINQ, GroupBy for-in + body locals, no owned on pipelines |
| `test-list-stdlib.c`       | Full stdlib List<T> validation |
| `test-array-to-list.cy`    | Array/slice `.ToList()`, `auto` deduction, `List(T*)` ctor |
| `test-list-conversions.cy` | `List<T>` → array/`dict`: `ToArray`, `CopyTo`, `ToJsonArray`, `ToDictBy` |
| `test-auto-conversions.cy` | Automagical round-trips: `List<T>.FromJson`/`ToJson`, `Map<String,V>.ToDict`/`ToJson`, `Map.Keys()`/`Values()` → `List<T>` |
| `test-object-initializer.cy` | C/C++23-style object initializers: `new T(args) { .field = value, ... }` (String/scalar/aggregate fields) |
| `test-typed-forin.cy`      | Typed for-in (`for (String s in arr)`, `for (int n in arr)`) |
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
| `classy-exceptions.cy`     | `try`/`catch`/`throw` (exceptions **on by default**) |
| `classy-safety.cy`         | JIT safety guards: null-ptr, div-by-zero, array/slice OOB (default-on with exceptions) |
| `classy-fetch.cy`          | HTTP/HTTPS client (`include/httpclient.h`): calls the PokéAPI over TLS, headers as a `dict`, `List<String>` |
| `classy-customers.cy`      | End-to-end typed JSON ingest: `(Customer)? rec` binds each record from `customers.json` into a `Map<int, Customer*>`, then runs 6 database-style queries (lookup, filter, group-by, aggregate, top-K) |
| `classy-restful.cy`        | SQLite-backed REST controller (`include/sqlite.h`): bound queries, `(User) row` binding, `List<dict>` → `ToDict()` JSON responses, transactions (`-l sqlite3`) |
| `classy-querybuilder.cy`   | LINQ-style `QueryBuilder<T>` (`include/sqlite.h`): fluent `Where`/`OrderBy`/`Take`/`Skip`/`Select`, materialising typed entities into an owning `List<T*>` (`-l sqlite3`) |
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
- **Collections (value first)** — prefer stack shells
  `auto xs = List<T>();` / `Map` / `Set`: the destructor frees the buffer at
  scope exit (RAII). `Where`/`Take`/`Copy`/… return **value** shells, so local
  LINQ pipelines need no `owned`/`delete`. Use `new List<T>` / `owned auto` when
  a collection must escape as a pointer. For collections of pointers, add
  `.owns()` (`.ownsValues()` / `.ownsKeys()` on `Map`) so destroying the shell
  also frees pointees — see *Element ownership* below.
- **Managed ownership (`owned` / `move` / `readonly`)** — opt a single-owner
  heap object into automatic cleanup: `owned auto x = new Box();` is released
  exactly once at the end of its scope with no `defer delete`. `move` transfers
  ownership (the source becomes a read-only view; the new owner does the
  cleanup), and `readonly` borrows a non-owning view. The static ownership
  pass proves single ownership and rejects use-after-move / view-deletes at
  compile time. Fully opt-in — see *Managed Ownership* in the feature list
  above.
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

Shipped since the early roadmap: typed lambdas (thin C function pointers),
**capturing lambdas as direct HOF args** (open-coded `Where`/`Filter`/`Map`/
`ForEach`/`Any`/`All`/`Find`/`Sort`/`Select` — see `LAMBDA-CAPTURE.md`),
generics (`List<T>` and user-defined collections, plus **generic functions**
with call-site type inference), the **first-class by-value collection idiom**
(stack `List`/`Map`/`Set`, move-return / prvalue bind, value-returning
`Where`/`Take`/`Copy`/`Select`/`GroupBy`/…, nested `List<List<T>>` /
`Map<G, List<V>>`, `GetMut` / `[]` buffer lvalues — see `BY-VALUE.md`,
`examples/classy-aurora-ops.cy`, `cy-validate/val-040`…`050`),
uncaught exceptions → **exit(1)** (not abort), shift-range safety traps,
`interface`/`Any<I>` erasure, default-on exceptions + safety guards, array/slice
→ `List<T>` conversion with lengths flowing into generics, **typed JSON binding**
(`(T) d` / `(T)? d` for class or struct, with `KeyException` on missing required
fields — including **collection fields** (`List<T>*` / `Set<T>*` from a JSON
array), Phase 2), a lightweight **SQLite wrapper** (`include/sqlite.h`) with
`dict`-row binding and `List<dict>` result sets, and a **gunicorn-style HTTP
server** library (`include/httpserve.h`).
In-progress directions include Phase 3 of the JSON binder (`Map<K,V>*` and
pointer-to-class elements, plus per-field annotations), full-expression temp
dtors, and AOT dead-code elimination.

The behavior described in this README is exercised by the executable validation
suite in **[`cy-validate/`](cy-validate/)** (**53** `val-*.cy` files; run
`sh cy-validate/run-validate.sh`). Bug regressions: `sh bugs/run-bugs.sh`.
Known rough edges and their workarounds are catalogued in
**[`cy-validate/SHORTCOMINGS.md`](cy-validate/SHORTCOMINGS.md)**.
Audit notes: [`CLASSYC-FINDINGS.md`](CLASSYC-FINDINGS.md),
[`CLASSYC-CLEANUP.md`](CLASSYC-CLEANUP.md).

Contributions, bug reports, and wild ideas are welcome!

---

## Limitations & Future Work

### What doesn't work / current limitations
- Single inheritance (`extends` / `super` / `virtual` methods). Use `interface` + `impl` + `Any<I>` (structural typing) instead — this combination covers all observed use-cases in ~8600 lines of examples.
- Class instances stored **by value** inside `List<T>`, `Set<T>`, and `Map<K,V>` are supported: elements (and `Map` keys/values) live inline and the collection runs each element's destructor on `delete`. Scalars, `String`, raw pointers, and `MyClass*` work as before. For **pointer** elements the collection is non-owning by default, but `.owns()` (`.ownsValues()`/`.ownsKeys()` on `Map`) makes `delete` also free the pointed-to objects. (See `GENERICSMEM.md` and the *Element ownership* section above.)
- `dict` arrays: JSON parsing builds them, `d.arr[i]` reads elements, `for-in`
  iterates both objects and arrays (runtime-tag dispatched), and `d.arr.length()` /
  `.count()` expose the size. The remaining gap is **array-literal assignment**
  (`d.tags = ["fast", "safe"];`) — unimplemented; use JSON or the runtime
  `dict_create_array`/`dict_array_append` helpers.
- Typed JSON binding `(T) d` / `(T)? d` covers scalars, `String`, nested
  class/struct members, and **collection fields** (`List<T>*` / `Set<T>*` from a
  JSON array — any class with a default ctor + `Add(T)`).  `Map<K,V>*` and
  pointer-to-class elements (`List<User*>*`) are **Phase 3** — the compiler
  reports a clear error directing you to write that field by hand.
- Stack value-construction works for plain classes (including those with constructor arguments): `Point p = Point(1, 2);` runs the constructor in place and `~Point()` at scope exit. **Generic collections are value-first too:** prefer `auto xs = List<int>();` / `Map<String,int>()` / `Set<int>()` — RAII frees the buffer at scope exit. Use `new List<T>` / `owned auto` only when a pointer identity must escape. Bare assign of `List`/`Map`/`Set` is banned (move-only); transfer with `move` or bind a by-value return. Transforms (`Where`/`Take`/`Copy`/`Select`/…) return **value** shells. Nested collections work: `List<List<T>>`, `Map<G, List<V>>`, and `GroupBy` → `Map<G, List<V>>` (Get copies a bucket; `GetMut` mutates in place). By-value class locals inside `for (… in map)` bodies (e.g. `Ship leader = bucket.First()` after GroupBy) share the same frame layout rules as ordinary nested blocks — loop vars and body locals do not alias.
- **`[]` on collections:** value and pointer receivers use the same Get/Set sugar — `list[i]`, `list_ptr[i]`, `map[k]`, `map_ptr[k]`. Plain `Point*` (no Get) stays C raw indexing. Nested collection dense buffers in the library use `*(data + i)` (and `memcpy` growth) so user-facing `List*[i]` sugar stays Get/Set.
- Exception names are resolved only at compile time. Runtime stores integer IDs only; there is no symbolic pretty-printing or `nameof`-style reflection for exceptions. The prelude ships `KeyException = 8` and `TypeException = 7` (used by the typed JSON binder); user code can extend the set with `enum { MyErr = 100 }`. Uncaught exceptions and uncaught safety traps print a diagnostic and **`exit(1)`** (not abort/core). Define `CY_EXC_ABORT=1` if you want core dumps for debugging.
- `List<T>.Sort` / `Set<T>` and a few other methods have minor edge-case limitations documented in the headers. Stack `Select<U>` works on pure value receivers (val-046); prefer nested blocks for RAII of pipeline intermediates when leaving function scope.
- **Lambdas / capture:** non-capturing lambdas lower to thin C function pointers. Capturing free locals is allowed **only** as a direct argument to List/Map/Set HOFs (`Where`/`Filter`/`Map`/`ForEach`/`Any`/`All`/`Find`/`Sort`/`Select`) via call-site open-coding — not as stored callbacks (`auto f = (int x) => x > thr` is an error). No fat closures / `std::function`-style escape yet. Array `.filter`/`.map` are deferred. See **[LAMBDA-CAPTURE.md](LAMBDA-CAPTURE.md)**.
- **Generic functions** (`T Max<T>(T a, T b)`) work with call-site type inference and multi-parameter templates, but two gaps remain: (1) **self-referential signatures** — a generic function whose return type or parameter type is itself a generic class instantiated on the function's own type param (`List<T> Sort<T>(List<T> xs)`) does not yet parse, because the `<T>` in the signature is not resolved as a placeholder the way it is inside generic class bodies; (2) **explicit type arguments at the call site** (`Max<int>(3, 5)`) are not yet supported — use inference (`Max(3, 5)`) or cast the arguments to disambiguate. Free `GroupBy` + UFCS already exists for List; self-referential signatures remain the blocker for more collection-level free generics.

### Want-to-have features (prioritized)
- ~~Automatic `defer delete` for `new`-bound locals, with `unowned` as the opt-out~~ **(landed)** — the static ownership analyzer in `src/ownership.c` tracks `malloc`-family and `new`-bound locals through a 5-state lattice, and `-fauto-release` synthesizes `defer free(p);` for definite leaks (see *Memory Management*). `unowned` is the opt-out at the declaration site.
- A working `attach <expr>;` paired with a lightweight dataflow / borrow-check pass: today `attach` parses and type-checks but emits no runtime call. Once the analysis is in, `attach` will adopt an externally-owned value into the current arena and the compiler will be able to prove every owning binding is matched by exactly one of `{scope-end defer, detach, attach-into-another-scope}`.
- ~~**Typed JSON binding — Phase 2**~~ **(landed)** — `(T) d` / `(T)? d` now
  populates `List<T>*` (and any `Add(T)`-protocol collection, e.g. `Set<T>*`)
  from a JSON array: allocates the collection, calls the default ctor, loops the
  array unwrapping each element (scalar / String private-copy / nested object
  via recursion), and calls `Add`.  The bound object owns the collection.  See
  `cy-validate/val-024-json-binding-collections.cy`.
- **Typed JSON binding — Phase 3**: extend collection binding to `Map<K,V>*`
  (needs `set(K,V)` dispatch) and pointer-to-class elements (`List<User*>*` from
  a JSON array of nested objects).  Then add an opt-in `Bindable` marker for
  per-field `required` / `optional(=default)` / `renamed("x")` annotations
  (C# `[JsonRequired]` / `[JsonPropertyName]` parity).
- ~~**First-class by-value collections**~~ **(landed)** — stack shells, move-return /
  prvalue bind, value-returning LINQ transforms (including **GroupBy** →
  `Map<G, List<V>>` and stack **Select**), nested `List<List<T>>` /
  `Map<G, List<V>>`, GetMut/`[]` lvalues, capturing **Find** / **Sort** /
  **Select**. Remaining: full-expression temp dtors. See `CLASSYC-CLEANUP.md`
  and `BY-VALUE.md`.
- ~~Uncaught exception → clean exit~~ **(landed)** — print + `exit(1)`; shift-range
  safety traps; `sh bugs/run-bugs.sh`.
- Richer `List<T>` / `Map<K,V>` syntactic sugar and initializer syntax (more Pythonic comprehensions, better literal support).
- Safe / typed JSON parsing helpers that return `Result<T, ParseError>` or throw on failure (beyond the current `asDict()` which can produce a null-ish dict on bad input).
- ~~Lightweight SQLite wrapper (`include/sqlite.h`) with automatic binding of `dict` rows and `List<dict>` result sets~~ **(landed)** — `Sqlite.open()`, `db->execute(sql, fmt, ...)`, `db->query(sql, fmt, ...) -> List<dict>*`, `db->prepare()` returning a real `Statement*` with overloaded `bind(int|long|double|const char*)`, RAII `Transaction*` for commit/rollback, `db->lastInsertRowId()`, and `SqliteError` exceptions on failure. SQL `NULL` round-trips as JSON `null` so `(T) row` bind-casts behave correctly. See `examples/classy-customers-rest.cy` for a Flask-style REST controller backed by an in-memory SQLite database.
- ~~Simple gunicorn-style HTTP server library~~ **(landed)** — `include/httpserve.h` plus `examples/http-serve.c` / `examples/classy-http-app.c` implement a shared `Request`/`Response` server with routing helpers; the two TUs link into one program (the driver enables MIR func-redef for ODR-style inline linkage across the boundary).
- **Generic function improvements**: (1) self-referential signatures (`List<T>* Sort<T>(List<T>* xs)`) — extend the placeholder-resolution path already used by generic class bodies to function signatures, unlocking collection-level algorithms; (2) explicit type arguments at the call site (`Max<int>(3, 5)`) — parse-time detection with check-time materialization, mirroring the inference path. These two close the gap between value-level generic primitives and the sort/map/reduce/hash/equality utilities the foundation is meant to enable.
- **Escaping closures** (optional later): fat `{fn,env}` or capture-as-args helpers for stored UI callbacks (`button.callback([this]{…})`). Collection HOF capture is already open-coded (Strategy A); this is only for plugins / event systems that need to *return* or *store* a capturing callable. See **[LAMBDA-CAPTURE.md](LAMBDA-CAPTURE.md)** §16.
- Optional pretty-printing / symbolic names for user-defined exceptions at debug time.

## Linking Shared Libraries (`-l` / `-L`)

The driver's `-l` / `-L` flags work just like `cc`/`ld`:

- `-l <name>` takes a **library name**, not a path. The driver builds `lib<name>.so` (or platform suffix) and searches the library directories.
- `-L <dir>` adds `<dir>` to the library search path.

On x86_64 Linux, `/lib64` and `/lib/x86_64-linux-gnu` are already on the built-in search path, so most system libraries just work with `-l` alone:

```bash
# Good: -l takes a name
./bin/classyc -I sketch -I include -l sqlite3 sketch/test-sqlite-classyc.cy -eg

# If the library lives in a non-standard location, add -L:
./bin/classyc -L /opt/mylib/lib -l mylib example.cy -eg
```

Passing a full path to `-l` (e.g. `-l /usr/lib/x86_64-linux-gnu/libsqlite3.so`) will fail with `cannot find library lib/usr/lib/...` because the driver prepends `lib` and appends the platform suffix to whatever you give it.

*Built with ❤️ on top of MIR. Original c2mir design by Vladimir Makarov.*
