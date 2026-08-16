# P6 sketch — GroupBy without `owned` (value Map shell)

## Today

```c
// map.h
Map<G, List<V>*>* GroupBy<G>(...) {
    Map<G, List<V>*>* result = new Map<G, List<V>*>();
    ...
    result->ownsValues();
    return result;
}

// call sites (neon / aurora / val-036…)
owned auto by = roster.GroupBy(keyFn);
```

Last major LINQ op in showcases that still forces `owned`.

## Ideal (blocked)

```c
Map<G, List<V>> GroupBy(...);   // nested value Lists
```

Probe: `Map<int, List<int>>` fails monomorph (move-only nested values in Map
dense buffer — copy/assign paths explode). → **Phase B / XL**.

## Phase A target (fixable, high leverage)

Return a **by-value** map whose **values are still `List<V>*` buckets** with
`ownsValues()`:

```c
Map<G, List<V>*> GroupBy<G>(G(*keySelector)(K, V)) const {
    auto result = Map<G, List<V>*>();
    result.ownsValues();
    for (...) {
        List<V>* bucket;
        if (!result.TryGet(gk, &bucket)) {
            bucket = new List<V>();
            result.Set(gk, bucket);
        }
        bucket->Add(...);
    }
    return move result;   // value Map shell; ~Map deletes each List*
}
```

Same for free `GroupBy<T,G>(List<T>* self, …)` / `ListGroupBy`.

### Call site

```c
auto by_sector = roster.GroupBy((Ship s) => s.SectorKey());
// no owned; RAII frees map table + owned List* buckets
for (auto k, bucket in by_sector) {
    printf("%s: %d\n", k, bucket->Count());
}
```

### Migration

| Old | New |
|-----|-----|
| `Map<G,List<V>*>* g = xs->GroupBy(fn); defer delete g;` | `auto g = xs.GroupBy(fn);` |
| `owned auto g = …GroupBy…` | `auto g = …` |
| `g->Get(k)->Count()` | `g.Get(k)->Count()` or `g[k]->Count()` |

Dual-ship optional: keep `GroupByPtr` returning `Map*` for one release.

## Why not jump to Phase B

* `List` is move-only; Map `Set`/`Get`/rehash assume bitwise value moves for V.  
* Nested `List` in dense `vals[]` needs move-on-rehash + no shallow copy of V —
  same class of work as “Map of move-only”.  
* Phase A removes **owned** from the common path without that ABI project.

## Validation

* New `val-047-groupby-value.cy`: stack GroupBy, no owned, no leak under
  valgrind/ASAN if available; bucket counts; scope exit frees.  
* Update val-033/034/036/037, neon-grid, aurora-ops.  
* Pointer element lists (`List<Pilot*>`) still non-owning inside buckets.

## Effort

M–L: header rewrite + call-site churn in validate/examples; risk is monomorph
of `Map<G, List<V>*>` **by value return** (should work — same as `Copy()` /
`Where` already returning value `Map`).

## Out of scope for Phase A

* `Map<G, List<V>>` true nested values  
* Changing GroupBy key type rules  
* Capturing GroupBy lambdas (separate; can open-code later)
