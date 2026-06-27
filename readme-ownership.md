# ClassyC — Memory & Ownership Model

ClassyC manages memory **statically**, with a **small runtime** and **no garbage
collector**. Instead of a GC, it layers a few complementary, mostly-automatic
mechanisms on top of ordinary C11, and a compile-time analyzer that proves each
heap resource is released exactly once.

The guiding rule: **you opt into as much automation as you want.** Plain C11 and
raw pointers always work as an escape hatch; on top of that sit arena-managed
`String`/`dict`, owning collections, and finally a single-owner `owned` / `move`
/ `readonly` layer that behaves like C#/Java references — but checked at compile
time, freed deterministically, and with zero GC.

---

## The layers at a glance

| Layer | What it manages | How it frees | Opt-in? |
|---|---|---|---|
| **C11** | `malloc`/`free`, raw pointers | you call `free` | always available |
| **String arena** | heap `String`s (`+`, `substr`, helper results) | automatically at scope/loop exit | on by default for `String` |
| **Value-semantic String fields** | a `String` stored in a **class field** | with the object (destructor) | automatic for class fields |
| **dict arena** | `dict` objects | `delete d` (one shot) | explicit |
| **Collections** | `List`/`Map`/`Set` + their elements | `delete` (+ `.owns()` for pointer elements) | explicit |
| **Objects** | `new T(...)` instances | `delete`, destructor cascade | explicit, or `owned` |
| **Managed ownership** | single-owner heap objects | auto at scope exit (no `defer delete`) | `owned` keyword |
| **Static analyzer** | proves the above is correct | reports leak / UAF / double-free | runs between check and gen |

---

## 1. Unmanaged C11 — the floor

Everything C does, ClassyC does. `malloc`/`free`, raw pointers, structs by
value. Nothing is automatic here, and nothing gets in your way:

```c
char *buf = malloc(256);
defer free(buf);            // optional: scope-bound cleanup
```

`unowned` opts a declaration *out* of every higher-level automation, handing
you full manual control:

```c
unowned Box* manual = new Box(99);   // the analyzer won't track or auto-free it
delete manual;                        // you own it; you delete it
```

---

## 2. Strings

A `String` is a heap, NUL-terminated, UTF-8 `char*`. There are **two** distinct
lifetime regimes for strings, and knowing which you're in is the key to the
whole model.

### 2a. The String arena (transient / local strings)

Every heap `String` produced by `+`, `.substr`, `.upper`, `.split`, `json(v)`,
`List<String>.join(...)`, or any helper that returns a `String` is **tracked in
a per-scope arena**. The compiler checkpoints at the start of each function (and
each loop iteration) and reclaims everything at scope exit. You write no cleanup:

```c
String greeting = "hi " + name;     // heap String, tracked
// ... used freely ...
// reclaimed automatically at function return — no free() needed
```

A `String` you `return` is automatically kept alive for the caller. An `atexit`
net guarantees a clean exit.

> **The `(String)` concat hint.** `"a" + i` is *pointer arithmetic* on a C
> string literal, not concatenation. To build a string from a literal + a value,
> hint the first operand: `(String)"a" + i`. Once one operand is `String`-typed
> (a variable, a field, a helper result), the rest of the `+` chain
> concatenates without further hints.
>
> ```c
> String s1 = name + " #" + i;        // name is String → concatenates
> String s2 = (String)"#" + i;        // literal start → needs the hint
> ```

### 2b. Value-semantic String fields (strings that live in objects)

A `String` stored in a **class field** outlives the scope that created it, so the
arena rule above doesn't apply. ClassyC gives class String fields **value
semantics**, like C# `string`:

- **Assignment copies.** `this.name = some_string` stores a *private heap copy*
  the object owns.
- **The destructor frees it.** When the object is destroyed (`delete`, scope
  exit, owning collection, …), its String fields are freed automatically.

```c
class User {
    int    id;
    String name;     // owned by the object: copied in, freed on destruction
    String role;
    User(int id, String name, String role) {
        this.id = id;
        this.name = name;       // takes a private copy
        this.role = role;
    }
    ~User() { /* compiler also frees name & role here */ }
};

User* makeUser(int id, String first, String last) {
    // first + " " + last is a transient arena String; the User copies it.
    return new User(id, first + " " + last, id == 1 ? "admin" : "viewer");
}
```

Because the copy is independent and always a freeable heap buffer, this is safe
across scopes, lists, and reassignment with **no leaks and no GC** — strings
simply live and die with their object. Reassigning a field frees the old buffer:

```c
u->name = "renamed";            // old buffer freed, new private copy owned
u->name = u->name + " (v2)";    // self-derived reassign: still exactly one live copy
```

### 2c. Manual escape / adopt

- **`detach <expr>`** removes a `String` (or pointer-to-class) from the current
  arena and yields the same value — now owned by whoever receives it. Use it to
  hand a transient String to a longer-lived owner without value-copy.
- **`attach <expr>`** is the inverse: adopt an externally-owned pointer into the
  current arena (reserved; parses + type-checks today).

---

## 3. dict — arena-backed JSON-like maps

A `dict` is a heterogeneous, string-keyed object backed by its own arena.
`dict` lifetime is **explicit**:

```c
dict cfg = { "host": "localhost", "port": 8080 };   // brace literal
defer delete cfg;                                    // frees the whole arena

auto big = new dict(256 * 1024);    // pre-sized arena
defer delete big;
```

`delete d` frees the dict and everything inside it in one shot. (Forgetting it
leaks the dict — the analyzer and valgrind will tell you.)

> Note: `String` fields *read out of* a dict point into the dict's arena, so the
> dict must outlive any such borrowed strings. To keep one past the dict, copy it
> (store into a class String field, which copies) or `detach` it.

---

## 4. Collections — explicit, with `.owns()` for elements

`new List<T>` / `Map<K,V>` / `Set<T>` are heap objects you own; pair them with
`defer delete` (or make the binding `owned`, see §6):

```c
List<int>* nums = new List<int>{ 1, 2, 3 };
defer delete nums;
```

For collections of **pointers**, the container is non-owning by default. Add
`.owns()` (`.ownsValues()` / `.ownsKeys()` on `Map`) to transfer element
ownership — `delete` then also destroys each element:

```c
auto library = new List<Track*>().owns();   // owns the Tracks
library->Add(new Track("Kashmir", 508));
defer delete library;                        // frees the list AND every Track
```

- **One owner per object.** Make the owning collection `.owns()`; leave sharing
  views and transform results (`Filter`/`Slice`/`Copy`/…) non-owning.
- **By-value elements** (`List<Track>`, not `List<Track*>`) are destroyed
  automatically via the `__destroy` intrinsic — no `.owns()` needed. Their
  `String` fields are freed too (value semantics, §2b).

> Strings stored into a collection inside a loop are kept alive at function
> scope (the analyzer disables per-iteration arena release for that loop), so
> `for (...) names->Add(label(i));` is safe to read after the loop.

---

## 5. Objects — `new` / `delete` and destructor cascades

`new T(...)` allocates and runs the constructor; `delete p` runs the destructor
(if any) and frees. `delete` is **null-safe** (deleting `null` is a no-op).

Ownership inside an object tree lives in its destructors: each node deletes the
children it owns. A `.owns()` collection field makes that a one-liner:

```c
class Team {
    String name;                  // owned String field (value semantics)
    List<Employee*>* members;     // the Team owns its employees
    Team(String n) { this.name = n; this.members = new List<Employee*>().owns(); }
    ~Team() { delete this.members; }   // cascade: frees every Employee
};
```

Arena-managed `any<I>(...)` handles use the same scope-bound model as Strings; a
handle you `return` is detached to the caller.

---

## 6. The managed ownership layer — `owned` / `move` / `readonly`

This is the opt-in, GC-like layer for **single-owner** heap objects. It makes the
common cases automatic while staying static and deterministic.

### `owned` — single owner, auto-released

Mark a local binding `owned` and the compiler guarantees it is released
**exactly once** at the end of its scope — no `defer delete`, no manual cleanup:

```c
owned auto repo = new Repo();    // released at scope exit, automatically
owned auto users = new List<User*>().owns();   // also frees its elements
```

The release runs through the same `defer` machinery, so it unwinds on every
`return` / `break` / `continue` path. A binding initialized by `move` is also
`owned` (ownership flowed in).

### `move` — transfer ownership (consuming)

`move x` transfers ownership out of `x` into the receiver. **`move` consumes its
source:** after `move x`, `x` is *dead* — any later use (read or write) is a
**compile-time error**. At runtime the source pointer is nulled, so the
single-owner cleanup never double-frees.

```c
owned auto u = new User(1, "Ada", "admin");
repo->add(move u);          // ownership u -> repo; u is now dead
// u->id  // <- compile error: use of moved value
```

To keep using the object, use the **new owner** (or take a `readonly` view
*before* moving). Conditional moves are handled correctly: on the path where you
didn't move, the binding is still cleaned up; on the path where you did, cleanup
is a no-op.

```c
Response* createUser(Repo* repo, int id, String name) {
    owned auto u = new User(id, name, "viewer");
    if (name.empty()) return new Response(400, "bad");  // u freed here
    repo->add(move u);                                   // u handed to repo
    return new Response(201, "ok");                      // u not freed (moved)
}
```

### `readonly` — non-owning borrow

`readonly y` yields the same pointer value but confers **no ownership**. A view
never releases anything and may be held anywhere — a local, a global, or a field
— with the single rule that it must not outlive its owner.

```c
auto v = readonly repo->find(id);   // borrow; repo still owns and frees it
print(v->name);                     // reading through the view is fine
```

### Putting it together

```c
owned auto x = new Box(1);   // x is the single owner
auto y = move x;             // ownership x -> y; x is dead
auto z = readonly y;         // z borrows a non-owning view of y
print(y->v, z->v);           // read through the owner or the view
// at scope exit: y released once; x and z release nothing
```

---

## 7. The static ownership analyzer

Between the type checker and code generator, ClassyC runs a CFG-based forward
dataflow over every function. It tracks each owned resource through a lattice:

| State | Meaning |
|---|---|
| **Unowned** | holds a value it does not own |
| **Owned** | owns a resource; must be released once |
| **Detached** | ownership transferred out (`detach`, return, store) |
| **Released** | explicitly released; further use is use-after-free |
| **MaybeOwned** | join-point conflict (owned on some paths, gone on others) |
| **Moved** | consumed by `move`; the binding is dead — any use is an error |

Tracked acquisitions include `malloc`/`calloc`/`realloc`/`strdup`/`strndup`,
ClassyC `new T(...)`, `owned` bindings, and `move` targets. The analyzer emits:

- `warning: leak` — still Owned at a function exit, never released/returned/stored
- `warning: potential leak` — MaybeOwned on some path
- `error: use-after-free` — used after Released
- `error: double-free` (and `double-free risk` on Detached / loop back-edges)
- `error: use of moved value` — used after `move` (consumed)

It is **interprocedural**: it infers per-parameter `((borrows))` / `((releases))`
and whether a function returns an owned pointer, then consults those summaries at
call sites. Null-check narrowing (`if (!p) ...`) refines paths.

### Driver flags

| Flag | Effect |
|---|---|
| `-fownership-report` | per-function/-class dump of every tracked allocation and where its ownership was disposed |
| `-fauto-release` | silently synthesize `defer free/delete` for definite leaks (legacy; `owned` supersedes it) |
| `-fcheck-whole-allocs` | stitch multiple `.cy` files into one virtual TU for whole-program ownership |
| `-v` | show each tracked candidate, summary, and synthesized cleanup |

`owned` bindings are auto-released **unconditionally** (it's the type's
contract), regardless of `-fauto-release`.

---

## 8. Keyword & operator reference

| Keyword | Position | Meaning |
|---|---|---|
| `new T(...)` | expression | allocate + construct on the heap |
| `delete p` | statement | run destructor + free (null-safe) |
| `defer <stmt>` | statement | run `<stmt>` at enclosing scope exit (LIFO) |
| `owned <decl>` | declaration prefix | single-owner, move-only, auto-released at scope exit |
| `move <expr>` | expression | transfer ownership; **consumes** the source (it becomes dead) |
| `readonly <expr>` | expression | non-owning read-only borrow/view |
| `unowned <decl>` | declaration prefix | opt out of all tracking; you manage it manually |
| `detach <expr>` | expression | remove value from the current arena; caller now owns it |
| `attach <expr>` | statement | adopt an external value into the current arena (reserved) |
| `.owns()` | method | collection owns (and frees) its pointer elements |

Soft-keyword notes: `move` / `readonly` / `detach` are **expression-leading**
soft keywords — they only shadow an identifier when they start an expression
(`a + move` reads the variable `move`; `move x` transfers). `owned` / `unowned`
are **declaration-prefix** soft keywords and stay usable as identifiers in
expressions.

---

## 9. Cheat sheet

- **Transient string?** Just use it — the arena frees it.
- **String in an object?** Just assign it — the object copies and owns it; freed
  with the object. Use `(String)` to hint a literal-led concat.
- **One heap object, scoped lifetime?** `owned auto x = new T(...);` — done.
- **Hand an object to a long-lived owner?** `owner->take(move x);` — `x` is then
  dead; use the new owner.
- **Read without taking ownership?** `auto v = readonly x;`.
- **Collection of pointers you own?** `new List<T*>().owns();`.
- **A `dict`?** `delete d;` (or `defer delete d;`).
- **Going fully manual?** `unowned` + raw `malloc`/`free`.

Run `-fownership-report` to see exactly what the analyzer proved, and build
AOT + `valgrind` to confirm zero leaks and zero double-frees.
