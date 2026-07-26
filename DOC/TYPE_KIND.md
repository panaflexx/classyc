# `type_kind` — type categories for memory, bugs, and optimizers

ClassyC classifies every type into a small lattice used by **semantic analysis**,
**ownership**, **collection monomorphization**, and (later) **midopt / list.h**.

This is not `nameof` / `typeof` (string identity). It answers:

> *How may this type be stored, copied, and destroyed?*

---

## Lattice

| Kind | Meaning | By-value `List<T>` | Ownership tracks? |
|------|---------|-------------------|-------------------|
| **POD** | scalars, enums, no-dtor pure data | ✅ | no (value) |
| **QUIET_VALUE** | dtor exists but does not release unique resources (counting/log, or `[[copyable_no_release]]`) | ✅ | no (value) |
| **ARENA_VALUE** | `String` / arena field policy | ✅ | arena rules, not free-pair |
| **UNIQUE_RESOURCE** | dtor frees/deletes unique stuff | ❌ use `T*` + `.owns()` | yes |
| **MOVE_ONLY** | `List`/`Map`/`Set` shells | ❌ (shells use `move`) | shell move rules |
| **POINTER** | raw pointer type | n/a (element is `T*`) | yes when owning |
| **OPAQUE** | cannot prove quiet vs unique | ❌ | conservative |

Attribute:

```c
[[copyable_no_release]]
class Box { int id; ~Box() { n++; } };  // forces QUIET_VALUE
```

Counting/log-only dtors with no raw owning pointers and no `free`/`delete` in
the dtor body are classified **QUIET_VALUE automatically** (attribute optional).

---

## Where it lives

| Piece | Role |
|-------|------|
| `class_type_meta` (`parse_ctx`) | cache per `N_CLASS`: kind + `[[copyable_no_release]]` |
| `class_type_meta_register` | end of class parse |
| `type_kind(c2m_ctx, type*)` | check-time API |
| `element_ok_byvalue_p` | List/Set/Map element legality |
| specialization gate | reject bad by-value elements early |
| `ownership.c` | skip quiet by-value candidates; report kinds |

Pipeline:

```text
parse (register kind) → check (type_kind API) → ownership (filter/report) → midopt/gen
```

---

## Bug detection (before gen / run)

1. **`List<Owns>`** where `~Owns` calls `free` → **UNIQUE_RESOURCE** → compile error  
   with fix-it: `List<Owns*>.owns()`.
2. **Double-free via `Where`/`Copy`** never reaches runtime for gated types.
3. **Ownership** does not invent free-pairs for QUIET/POD by-value locals → less noise.
4. **OPAQUE** with a dtor forces an explicit choice (DTO, owns pointers, or waiver).

---

## Fun / useful errors

Gate messages include the **kind name** and a **kind-specific fix-it**:

```text
type 'Owns' is UNIQUE_RESOURCE and cannot be a by-value List element;
List stores elements by bitwise copy ... each copy would run ~Owns.
this type's destructor releases unique resources; use List<T*>.owns() ...
```

Ownership reports annotate candidates:

```text
p = new(...)  [POINTER]  at foo.cy:12
```

Same vocabulary in docs, gate, and ownership → faster mental model.

---

## Optimization hooks

| Kind | Optimizer opportunity |
|------|------------------------|
| **POD** | bulk `memcpy` of slots; elide `__destroy`; no ownership CFG |
| **QUIET_VALUE** | same memcpy; keep dtor calls but no free-pair analysis |
| **ARENA_VALUE** | String-aware copy / arena reclaim paths |
| **UNIQUE_RESOURCE** | force pointer/`owned` path; never treat assign as share |
| **MOVE_ONLY** | keep move-only assign ban; steal buffer on `move` |
| **POINTER** | `.owns()` destroy loop vs non-owning view elision |

`type_kind_bitwise_copy_safe_p` / `type_kind_ok_byvalue_element_p` are the
predicates midopt and (later) `list.h` intrinsics should call.

---

## Algorithm (class)

```text
if [[copyable_no_release]] → QUIET_VALUE
if name is __generic_List_/Map_/Set_ → MOVE_ONLY
scan fields: raw ptr?, String?, nested UNIQUE?
scan dtor body: free/delete/fclose/… ?

no dtor:
  nested UNIQUE → UNIQUE_RESOURCE
  String fields only → ARENA_VALUE
  else → POD

has dtor:
  dtor releases OR nested UNIQUE OR raw ptr field → UNIQUE_RESOURCE
  else → QUIET_VALUE   // side-effect-only dtor
```

---

## Future

- `is_byvalue_element_ok<T>()` / `type_kind<T>()` intrinsics for `list.h`
- Deeper nested class fold at check time
- Sugar: `List<ObjectClass>` → owning `List<ObjectClass*>` for UNIQUE kinds
- Midopt: elide destroy loops for POD element specializations
