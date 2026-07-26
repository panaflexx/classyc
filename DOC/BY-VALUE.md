# By-value classes & collections — plan (C++ architecture)

**Product stance:** ClassyC collections follow a **C++ `vector` / RAII** model, not C# GC reference-type `List`.

- The **container** is an owning value (stack or `owned` heap) that owns a **heap buffer**.
- **Elements** are stored **inline** (by value) or as **pointers** (`T*` + optional `.owns()`).
- Transfer with **`move`**; bare assign of `List`/`Map`/`Set` is an **error**.
- Domain / identity objects stay **`List<T*>.owns()`**.
- Small DTOs / POD stay **`List<T>` by value**.

This file is the implementer’s plan: what works, memory rules, remaining phases, file touch points, tests, and code samples.

Related: [`GENERICSMEM.md`](GENERICSMEM.md), [`include/list.h`](include/list.h), [`include/map.h`](include/map.h), `cy-validate/val-038-*.cy`, `val-039-*.cy`.

---

## 1. Target house style (what users write)

### 1.1 Vector of values (DTO / POD)

```c
class LapSample {
    int lap;
    int ms;
    int faction;

    LapSample(int lap, int ms, int faction) {
        this.lap = lap;
        this.ms = ms;
        this.faction = faction;
    }
    // Prefer no user dtor, or only “quiet” counting / no free of shared bits.
    // String fields are OK (arena). Do not free unique malloc in ~T if List
    // will bitwise-copy you.
};

int main() {
    auto laps = List<LapSample>();              // stack List, owns buffer
    laps.Add(LapSample(1, 59840, 0));           // prvalue → buffer (P0)
    laps.Add(LapSample(2, 60112, 1));

    owned auto quick = laps.Where((LapSample s) => s.ms < 62000);
    // Where returns heap List*; non-owning of nothing extra for POD copies

    for (auto s in laps)
        printf("L%d %d\n", s.lap, s.ms);

    // ~quick (owned) → delete Where-result list
    // ~laps → __destroy each LapSample + free(buffer)
}
```

Brace-init on heap list:

```c
owned auto xs = new List<LapSample>{
    LapSample(1, 59840, 0),
    LapSample(2, 60112, 1)
};
```

### 1.2 Vector of unique owning pointers (domain objects)

```c
class Pilot {
    int id;
    String callsign;
    Pilot(int id, String callsign) {
        this.id = id;
        this.callsign = callsign;
    }
    ~Pilot() { printf("~Pilot %s\n", callsign); }
};

int main() {
    auto grid = List<Pilot*>().owns();         // stack List; owns pointees
    grid.Add(new Pilot(1, "AURORA"));
    grid.Add(new Pilot(2, "HEXFIRE"));

    for (auto p in grid)
        p.Banner();

    // ~grid: delete each Pilot*, then free pointer buffer
}
```

Non-owning view (same pointees, one owner):

```c
owned auto aces = grid.Where((Pilot* p) => p.IsAce());
// delete aces → frees List shell only; Pilots still owned by grid
```

### 1.3 Move the container (not shallow assign)

```c
auto a = List<int>();
a.Add(1);
a.Add(2);

auto b = List<int>();
b = move a;                 // buffer ownership → b; a zeroed
// a.Count() == 0
// b owns data

auto c = move b;            // transfer again
// ~c frees once; ~b / ~a no-op on buffer
```

Illegal (and should stay illegal):

```c
auto a = List<int>();
auto b = List<int>();
b = a;                      // ERROR: shallow copy would double-free
List<int> c = a;            // ERROR: copy-init same problem
```

Explicit second list object:

```c
owned auto clone = a.Copy();  // new heap List + bitwise element copies
// caller owns clone
```

### 1.4 Heap container when you need `List*`

```c
List<int>* make_scores() {
    owned auto xs = new List<int>();
    xs.Add(10);
    xs.Add(20);
    return move xs;           // ownership leaves function
}

int main() {
    owned auto scores = make_scores();
    scores.Add(30);
} // delete scores → ~List
```

### 1.5 Map / Set (same architecture)

```c
auto board = Map<String, int>();
board.Set("AURORA", 2140);
board["HEXFIRE"] = 1912;

auto by_id = Map<int, Pilot*>().ownsValues();
by_id.Set(1, new Pilot(1, "AURORA"));
// ~by_id: delete values if ownsValues; free tables

auto tags = Set<String>();
tags.Add("nova");
// String content hash/eq already special-cased
```

---

## 2. Memory diagram

```text
// auto xs = List<LapSample>();  xs.Add(...); xs.Add(...);

stack frame:
  xs { data*, length, capacity, _owns_ptrs }
         │
         ▼
      heap:  [ LapSample | LapSample | ... empty slots ... ]
                 ↑ inline values (sizeof(LapSample) each)

// auto grid = List<Pilot*>().owns(); grid.Add(new Pilot(...));

stack:
  grid { data*, length, ... _owns_ptrs=1 }
         │
         ▼
      heap:  [ Pilot* | Pilot* | ... ]
                 │         │
                 ▼         ▼
              heap Pilot  heap Pilot
```

Scope exit LIFO (typical):

```text
~owned results first (Where/Copy lists)
~grid: for i: delete data[i]; free(data)
~xs:   for i: __destroy(data[i]); free(data)
```

---

## 3. What’s already done

| Feature | Where / notes |
|---------|----------------|
| By-value `List<T>` buffer + ABI | compiler + `list.h` |
| `__destroy` on dtor / Clear / Set / RemoveAt | `list.h`, `map.h`, `set.h` |
| `is_pointer<T>` + `.owns()` | compiler intrinsic + headers |
| Stack `List<T> a;` / `auto a = List<T>()` | RAII in `create_decl` + auto inference for `ClassName()` |
| Move-only List/Map/Set assign | `class_is_move_only_collection_p`, `move` class path |
| `ClassName(args)` prvalue construction | check/gen `N_CALL` when callee is class |
| Brace-init `new List<Pt>{ Pt(...), ... }` | uses prvalue path + Add protocol |
| Tests | `val-038-list-stack-byval.cy`, `val-039-brace-init-move.cy`, `val-015-*`, `examples/test-list-byval.cy` |
| **P0 done** — prvalue temp dtors at consuming call | `class_prvalue_call_p` + `gen_class_temp_dtor` in `classyc.c`; `val-056` |
| **P1 done** — by-value element gate + `[[copyable_no_release]]` | parse-time class registry + gate in `get_or_create_specialization`; `val-056` |
| **P5 done** — C-array element dtors at scope exit | `create_decl` RAII for `TM_ARR` of dtor-class (constant bound); `val-056` |

### Helpers already in `classyc.c`

```c
// Approximate names / roles — search for these when coding:
find_class_dtor_def(c2m_ctx, class_def);
class_has_dtor_p(c2m_ctx, type);
class_is_move_only_collection_p(c2m_ctx, type);
//   true for __generic_List_*, __generic_Map_*, __generic_Set_* with dtor
```

Class prvalue path (sketch of current behavior):

```c
// check N_CALL: callee is N_ID naming a class
//   → type = TM_CLASS, builtin_call_p, def_node = ctor
//   → update_call_arg_area_offset for temp slot

// gen N_CALL: class prvalue
//   → stack slot in call-arg area
//   → memset 0, call __ctor_T(this, args)
//   → yield MIR_OP_MEM of aggregate
```

**~~Gap~~ (FIXED):** temp is **not** always registered for end-of-full-expression `~T` after the value has been copied into a list (or after a call). POD is fine; nontrivial `~T` is the bug class for **P0**.

**Resolution (compiler-validated):** `Add(T(...))` / brace-init were already balanced (the container adopts the temp's bits and destroys it once). The real leaks were **by-value call arguments** (`take(Box(7))`) and **prvalue method receivers** (`Pt(1,2).getX()`) — fixed by emitting `~T` for the temp right after the consuming call, except (a) adopting protocol methods (`Add`/`Set`/`Insert`/`Push`/`Enqueue` on a move-only collection — the container owns it) and (b) calls returning a pointer (result may alias the temp — kept alive, conservative).

---

## 4. Remaining phases

> **Status: P0, P1, P2 and the P5 array item are landed** (val-056 + gate
> diagnostics; suite 58/58 green).  Remaining: P3 (optional clone polish),
> P5 leftovers (bare `T(...);` statement temps, ctor-arg prvalues).

### P0 — Class prvalue temporary lifetimes  ✅ **DONE** (refined scope)

> **What validation showed:** the doc's original worry (`Add(T(...))` temp
> leaks) did **not** reproduce — Add/brace-init were already balanced. The
> actual leaks, confirmed with strict ctor/dtor probes
> (`sketch/probe-p0-prvalue-lifetime.cy`, `probe-p0b-arg-leak.cy`), were:
> `take(Box(7))` prvalue call args and `Box(9).getId()` prvalue receivers.
> Both now destroy the temp exactly once after the consuming call
> (`val-056`).  Remaining known edges (documented, low value): a bare
> `Box(7);` expression-statement temp still leaks; class-prvalue args to a
> *ctor* of another prvalue (`Box(Box2(1))`) are not destroyed; calls
> returning pointers intentionally keep the temp alive.

#### Goal

`T(...)` as a temporary is either:

1. **Relocated** into destination storage (list buffer, param, field) and **not** double-destroyed, or  
2. Destroyed exactly once at **end of full-expression**.

C++-ish rule (simplified): construct temp → use (copy/relocate into owner) → destroy temp if still “alive”.

#### Preferred implementation for `List.Add` / brace-init

**Construct-in-place into the destination slot** when the argument is a pure prvalue:

```c
// Ideal lowering for: xs.Add(LapSample(1, 2, 0));
// 1. EnsureCapacity
// 2. Construct LapSample directly at &xs.data[xs.length]  (placement ctor)
// 3. length++
// No separate temp → no temp dtor problem
```

If placement into `data[i]` is hard in today’s MIR path, fallback:

```c
// 1. ClassName(args) → stack temp (existing)
// 2. bitwise relocate temp → data[length]
// 3. memset temp 0  (moved-from: ~T is no-op if T only frees owned pointers)
// 4. OR register ~T on temp at full-expression end WITHOUT zeroing if we deep-copied
```

For List’s bitwise model, **moved-from zeroing** matches container `move` and is simpler than full-expr dtor lists for every Add.

#### Full-expression destroy (needed for call args)

```c
void take(LapSample s);   // by value: owns its own copy in param slot

take(LapSample(1, 2, 0));
// After call returns, the prvalue temp (if any) must not leak.
// Param slot is destroyed at end of take() — that’s the callee’s stack.
```

#### Sketch: register temp dtor with existing defer machinery

Gen already has:

```c
VARR (node_t) * defer_stmts;
gen_run_defers(c2m_ctx, from);
// N_BLOCK / N_RETURN / break / continue unwind defer_stmts
```

Possible approach:

```c
// At gen of class prvalue N_CALL (when type has user dtor):
//   after constructing temp at slot S:
//   synthesize a dtor call node that takes address of S
//   push onto a "full_expr_temps" stack tied to current statement
//
// At end of gen for N_EXPR statement (expression-statement):
//   run full_expr_temps LIFO (destroy temps)
//
// When temp is known moved-from (zeroed into list):
//   do not push dtor (or push no-op)
```

Find expression-statement gen (`N_EXPR`) and block boundaries.

#### Files

| File | Work |
|------|------|
| `src/classyc.c` | prvalue gen: moved-from vs full-expr dtor; optional place-into-Add |
| `include/list.h` | no API change unless special `Add` path |
| `cy-validate/val-040-class-prvalue-lifetime.cy` | **new** |

#### Test sketch (`val-040`)

```c
int ctors = 0, dtors = 0;

class Box {
    int id;
    Box(int id) { this.id = id; ctors++; }
    ~Box() { dtors++; }
    int getId() { return id; }
};

void take(Box b) {
    check(b.getId() == 7, "param received");
}

int main() {
    // Named stack: 1 ctor, 1 dtor at scope end
    {
        Box a = Box(1);
        check(ctors == 1, "named ctor");
    }
    check(dtors == 1, "named dtor at scope exit");

    // Prvalue arg: ctor for temp/param; dtor when param dies (and temp if separate)
    int d0 = dtors;
    take(Box(7));
    check(dtors > d0, "take(Box(7)) ran dtor(s)");

    // List stores copies; each Add should not leak temps
    d0 = dtors;
    int c0 = ctors;
    {
        auto xs = List<Box>();
        xs.Add(Box(10));
        xs.Add(Box(20));
        check(xs.Count() == 2, "list has 2");
        // At least the two elements die with xs; temps must not leave leftover alive
    }
    check(dtors - d0 >= 2, "list elements destroyed");
    // Optional stricter: (dtors - d0) == (ctors - c0)  // exact match once P0 done

    return failed;
}
```

#### Done criteria (P0) — status

- [x] `Add(T(...))` with counting dtor: no leak of temps; no double-free *(was already balanced — verified)*
- [x] Brace-init `new List<T>{ T(...), T(...) }` same *(verified)*
- [x] `take(T(...))` destroys *(fixed — temp dtor after the call)*
- [x] `val-038` / `val-039` still pass
- [x] `examples/test-list-byval.cy` still pass

---

### P1 — Element relocatable / triviality gate  ✅ **DONE**

> Shipped as an **error** with the `[[copyable_no_release]]` opt-out, as
> designed below.  Implementation notes: the gate lives in
> `get_or_create_specialization` (single choke point for `List`/`Set`/`Map`)
> and resolves element classes through a **parse-time class registry**
> (`parsed_classes` / `copyable_no_release_classes` VARRs in `parse_ctx`) because
> class symbols only enter check-time scopes after specialization runs.
> Map checks both `K` and `V`; pointer elements and nested `__generic_*`
> collections are exempt.  Counting-dtor test/example classes were
> annotated: `val-015` (Tag/Item/Key), `val-038`/`val-039` (Pt),
> `val-052` (Ship), `examples/test-list-byval.cy` (Track),
> `examples/classy-aurora-ops.cy` (Ship),
> `examples/classy-space-trader-2000.cy` (Planet/Outpost/Trader).
> Regression coverage: `val-056` (positive) +
> `sketch/probe-p1-relocate-gate.cy` (negative — must not compile).
> The original double-free repro (`Where` on `List<Owns>` → SIGABRT,
> `sketch/probe-p1b-transform-doublefree.cy`) is now rejected at compile time.

#### Goal

Refuse (or hard-warn) **`List<T>` / `Map` values/keys / `Set<T>` by value** when `T` is not safe to bitwise relocate.

List internals always do the moral equivalent of:

```c
data[j] = data[j - gap];     // Sort / Insert / EnsureCapacity
T item = data[i];            // Get / Where
```

If `~T` frees a unique resource still aliased in two slots → **double-free**.

#### Classification

| Kind | Rule | Examples |
|------|------|----------|
| **Trivial / safe** | no user dtor, or only scalars + `String` | `int`, `LapSample` POD, `Pt` with quiet `~` optional |
| **Attribute opt-in** | `[[copyable_no_release]]` | intentional POD-with-dtor-for-counting |
| **Unsafe by value** | user dtor frees unique resource, or owns raw ptr | `FileHandle`, `Buffer` with `free` in `~` |

#### Compiler behavior (recommended)

```c
// At List<T> / Set<T> / Map K,V specialization or first method check:
if (element is TM_CLASS
    && class_has_dtor_p(T)
    && !is_copyable_no_release(T)
    && !is_move_only_collection(T)) {   // don't confuse List itself
  error(...,
    "type '%s' has a destructor and is not marked [[copyable_no_release]]; "
    "List/Set/Map store elements by bitwise copy, so each copy would run ~T. "
    "Use List<%s*>.owns(), or mark [[copyable_no_release]] if the destructor "
    "does not release unique resources (counting/log-only)",
    name, name);
}
```

Attribute (parser + type flag):

```c
[[copyable_no_release]]
class Probe {
    int id;
    ~Probe() { /* counting only — no free of shared bits */ }
};
```

#### Heuristic without full attribute support (MVP)

```c
// class_is_safe_byvalue_element_p(type):
//   !class_has_dtor_p(type)  → true
//   else if name has attribute flag → true
//   else → false   // or warn-only first week
```

Ship **error** once attribute exists; **warning** is OK for first PR if you want less breakage for counting-dtor test types (`Track` in `test-list-byval`).

Counting-dtor tests should use:

```c
[[copyable_no_release]]
class Track {
    int id;
    ~Track() { count++; }
};
```

#### Files

| File | Work |
|------|------|
| `src/classyc.c` | parse attribute; `class_is_safe_byvalue_element_p`; gate in specialization or List method check |
| `include/list.h` | comment on allowed `T` |
| `cy-validate/val-041-byvalue-element-gate.cy` | **new** |

#### Test sketch (`val-041`)

```c
// Positive: POD
class Pod { int x; Pod(int x) { this.x = x; } };
// List<Pod> compiles

// Positive: attribute
[[copyable_no_release]]
class Counted {
    int x;
    Counted(int x) { this.x = x; }
    ~Counted() { /* count++ */ }
};
// List<Counted> compiles

// Negative (expect compile error): 
// class Owns {
//     char* p;
//     Owns() { p = (char*)malloc(8); }
//     ~Owns() { free(p); }
// };
// List<Owns>* bad = new List<Owns>();  // ERROR
//
// Fix:
// List<Owns*>* ok = new List<Owns*>().owns();
```

For automated suite: keep negative case in a separate file run with “expect fail”, or only document until runner supports expected-fail.

#### Done criteria (P1) — status

- [x] Non-relocatable + dtor as List element → diagnostic
- [x] POD / `String` / pointer elements unchanged
- [x] Existing byval tests annotated `[[copyable_no_release]]` if they have counting dtors
- [x] Map/Set share the same helper

---

### P2 — Documented memory contract + showcase  ✅ **DONE**

#### House style block (paste into GENERICSMEM + README)

```c
// ── Values / DTOs → by-value List (vector<T>) ─────────────────────────
auto samples = List<LapSample>();
samples.Add(LapSample(1, 59840, 0));

// ── Identity / resources → owning pointers (vector<unique_ptr<T>>) ────
auto people = List<Pilot*>().owns();
people.Add(new Pilot(1, "AURORA"));

// ── Transfer container ────────────────────────────────────────────────
auto other = move samples;

// ── Explicit second container ─────────────────────────────────────────
owned auto clone = people.Copy();  // new list; still one Pilot owner = people

// ── Heap container when you need List* ────────────────────────────────
owned auto heap = new List<int>();
```

#### Memory rules table

| Write this | Owns buffer? | Owns pointees? | Cleanup |
|------------|:------------:|:--------------:|---------|
| `auto xs = List<T>()` | yes | values in buffer | `~List` |
| `auto xs = List<T*>()` | yes | no | free buffer only |
| `auto xs = List<T*>().owns()` | yes | yes | `delete` each + free buffer |
| `owned auto xs = new List<T>()` | yes | as above | auto `delete xs` |
| `b = move a` | transferred | transferred | one owner |
| `xs.Copy()` / `Where` / … | **new** list | **never** copy `.owns()` | caller frees list shell |

#### Update stale docs / examples

| Location | Change |
|----------|--------|
| `GENERICSMEM.md` TL;DR | Stop saying “classes are only by pointer”; dual path (DTO by value / domain by pointer) |
| `GENERICSMEM.md` “value construction” | Reflect stack List + move + prvalues |
| `examples/classy-neon-grid.cy` | Remove or rewrite caveats: brace-init works; enum-as-int may still be recommended; Sort+String mostly OK with arena |
| `README.md` collections blurb | Point at this file + GENERICSMEM |
| `include/list.h` banner | Keep move-only + brace samples (already partially done) |

#### Done criteria (P2) — status

- [x] GENERICSMEM TL;DR matches dual path
- [x] neon-grid comments not lying
- [x] Cross-link: README → `BY-VALUE.md` / GENERICSMEM

---

### P3 — Explicit clone polish (optional)

Keep **no** silent deep assign. Optional:

```c
// Already:  List<T>* Copy();
// Optional alias:
// List<T>* Clone() { return this.Copy(); }

// Future sugar (language):
// auto b = copy a;   // → a.Copy(), never shallow
```

Not required for C++ architecture correctness.

---

### P4 — Map/Set share P1

Call the same `class_is_safe_byvalue_element_p` for:

- `Set<T>` element type  
- `Map<K,V>` for both `K` and `V` when class-by-value  

Document: class keys = memcmp / byte `==` only; prefer `int`/`String` keys.

---

### P5 — Edge polish (partially done)

| Item | Notes |
|------|--------|
| `arr[i] = T(...)` on C arrays | ✅ elements now destroyed at scope exit (constant bound; VLAs exempt) — `val-056` |
| Nested ctor `T(-p.getX(), …)` | ✅ already works (verified with probe — struck) |
| Bare `T(...);` expression-statement temps | still leaks (low value) |
| Class-prvalue arg to another prvalue's ctor (`Box(Box2(1))`) | not destroyed (edge) |
| `ToJson` on `List<class>` | keep `ToJsonArrayBy`; no auto object schema required |
| Sort + element with String | arena makes it usually OK; don’t promise deep move semantics |

---

## 5. Non-goals (C++ architecture)

Do **not** implement unless product stance changes:

1. Silent `b = a` deep copy of List (surprising O(n), still not C# share).
2. Copy-on-write / refcounted List buffers (C#-like share).
3. GC for collections.
4. Full C++ rule-of-five / implicit copy ctor generation for all classes.
5. By-value List of types that `free` unique resources in `~T` without relocation rules.

---

## 6. Implementation order (tomorrow’s sequence)

```text
Day 1 morning   P0: reproduce lifetime bug with strict dtor-count test
Day 1           P0: relocate-into-Add or full-expr temp dtor
Day 1 afternoon val-040 green; re-run val-038/039

Day 2 morning   P1: attribute + class_is_safe_byvalue_element_p
Day 2           P1: gate at specialization; fix counting tests with attribute
Day 2 afternoon val-041; Map/Set same helper

Day 2/3         P2: GENERICSMEM + neon-grid + README cross-links
```

If only one day: **P0 only** — highest correctness value.

---

## 7. Touch map (`classyc.c`)

| Concern | Search / area |
|---------|----------------|
| Class prvalue check | `ClassName(args) value construction` in `N_CALL` check |
| Class prvalue gen | same comment in `gen` `N_CALL` |
| Stack List RAII | `create_decl` RAII block, `ctor_call` / `dtor_call` |
| Move-only collections | `class_is_move_only_collection_p`, `N_ASSIGN`, `N_MOVE` |
| Defer / scope dtor | `defer_stmts`, `gen_run_defers`, `N_BLOCK` |
| Specialization | `get_or_create_specialization` |
| Protocol Add | `find_class_protocol_method(..., "Add", 1)` |
| Brace-init gen | `N_NEW` brace / object-initializer branch |

Build:

```sh
cmake --build . --target classyc -j$(nproc)
./bin/classyc -g -I include cy-validate/val-038-list-stack-byval.cy -eg
./bin/classyc -g -I include cy-validate/val-039-brace-init-move.cy -eg
# after P0:
./bin/classyc -g -I include cy-validate/val-040-class-prvalue-lifetime.cy -eg
```

**Do not use git** unless you choose to (per session norms).

---

## 8. Success criteria (project-level)

Users can write:

```c
// DTO path — no new per element, no delete, no double-free
auto laps = List<LapSample>();
laps.Add(LapSample(1, 59840, 0));
owned auto best = laps.Where((LapSample s) => s.ms < 62000);

// Domain path
auto grid = List<Pilot*>().owns();
grid.Add(new Pilot(1, "AURORA"));

// Transfer
auto other = move laps;

// Forbidden footguns caught by compiler
// b = a;                         // collection shallow assign
// List<OwnsMalloc> xs;           // non-relocatable by-value element (P1)
```

And dtor counts under prvalue Add are exact (P0).

---

## 9. Quick reference — C++ vs ClassyC

| C++ | ClassyC |
|-----|---------|
| `std::vector<Lap> v;` | `auto v = List<Lap>();` |
| `v.push_back(Lap{...});` | `v.Add(Lap(...));` |
| `std::move(v)` | `move v` |
| `std::vector<std::unique_ptr<P>>` | `List<P*>().owns()` |
| `std::vector<P*>` non-owning | `List<P*>()` |
| copy ctor / deleted copy | move-only + `Copy()` |
| RAII | `~List` / `owned` / scope dtor |

---

## 10. Notes from prior session (implementation gotchas)

1. **Ban only collections**, not every class with a dtor, for assign/copy-init — otherwise `List<Pt>` element assigns inside Sort/Add explode. Use `class_is_move_only_collection_p` (`__generic_List_*` etc.).

2. **`rhs_node` is not in scope** at the shared `assign:` gen label — use `NL_EL(r->u.ops, 1)` when checking `N_MOVE` RHS.

3. **Comment `List<T*>` in block comments** — `*/` inside `T*>` closes the C comment early. Spell out “T-star” or use line comments.

4. **MIR assert `collect_addr_uses`** showed up when gen state was wrong (e.g. calling dtor helpers with bad `item` pointers during assign). Guard `dd->u.item != NULL` before emitting dtor calls in gen.

5. **Auto + class value:** `auto x = List<int>()` works via class-name call detection in auto inference — keep that path when refactoring prvalues.

6. **Parse-time resolution for the P1 gate:** class symbols enter check-time scopes only *after* `get_or_create_specialization` runs, so the gate resolves element classes through a parse-time registry (`parsed_classes` / `copyable_no_release_classes` VARRs fed by the declaration parser), not `find_def`.

7. **Every new `struct expr` field must be initialized in `create_expr`** (reg_malloc is not zero-fill).  `hoist_call_p` was added without an initializer; garbage bits randomly marked plain calls as loop-invariant-pure, and the while back-edge then reused a stale pre-header value → infinite loop (`gcc/20010129-1.c`).  See CLASSYC-FINDINGS.md §10.

---

## 11. Checklist (copy into PR / notes)

```text
P0 prvalue lifetime
[x] Strict dtor-count repro            (sketch/probe-p0-*.cy)
[x] Relocate or full-expr destroy      (temp dtor after consuming call)
[x] val-056 green                      (val-040/041 names were taken)
[x] val-038/039/test-list-byval green

P1 element gate
[x] class_is_safe_byvalue_element_p equivalent (parse-time registry + gate)
[x] Gate List/Set/Map specialization
[x] Annotate counting-dtor test types
[x] val-056 + negative probe (probe-p1 must not compile)

P2 docs
[x] GENERICSMEM TL;DR dual path          (+ gate row/rule added)
[x] neon-grid caveats updated            (comment now reflects enforcement)
[x] README link to BY-VALUE.md           (Memory Management → .owns() section)
```

---

*Last updated from the by-value / stack-List / move / brace-init workstream. Architecture: C++ vector + unique ownership, not C# GC List.*
