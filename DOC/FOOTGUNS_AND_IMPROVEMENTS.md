# ClassyC Footguns & Improvements Summary

## Current State: What Works Well ✅

1. **`__destroy` intrinsic for by-value elements** - Collections automatically clean up by-value class elements
2. **Automatic String arena management** - No manual cleanup needed for String allocations
3. **`defer` statement** - LIFO cleanup on all scope exits (return, throw, break, normal)
4. **Type-safe generics** - `List<T>`, `Map<K,V>`, `Set<T>` with full type checking

## Top 5 Footguns & Solutions

### 1. **Pointer Collections Require Manual Cleanup** ⚠️

**Problem:**
```c
List<Track*>* library = new List<Track*>();
library->Add(new Track(...));
defer delete library;  // ❌ LEAKS all Track* objects!

// Must remember:
for (auto t in library) delete t;  // manual loop
defer delete library;
```

**Solution:** Implement `is_pointer<T>` intrinsic (see `COMPILER_INTRINSIC_is_pointer.md`)

```c
List<Track*>* library = new List<Track*>.owns();  // owns pointers
library->Add(new Track(...));
defer delete library;  // ✅ auto-deletes all Track* objects
```

**Status:** Design complete, awaiting compiler implementation

---

### 2. **String Ownership is Invisible** 

**Problem:**
```c
String s1 = "hello";              // Tier 0: literal
String s2 = s1 + " world";        // Tier 1: scoped arena
String s3 = make_greeting();      // Tier 2: returned (release_keeping)
char* s4 = s2.detach();           // Tier 3: manual (must free)

// Easy to leak:
void bad() {
    String s = compute().detach();  // forgot to free(s) - LEAK!
}
```

**Current workaround:** Document the tiers clearly (already in `SHORTCOMINGS.md`)

**Future solution:** Type-level ownership tracking
```c
String s = "hello";           // arena-managed
OwnedString o = s.detach();   // must free
defer free(o);
```

---

### 3. **`defer delete` Boilerplate Everywhere**

**Problem:**
```c
void handler(Request* req) {
    auto resp = new Response();
    defer delete resp;              // boilerplate
    
    auto data = new List<Item*>();
    defer delete data;              // noise
    
    auto cache = new Map<String, int>();
    defer delete cache;             // tedious
}
```

**Solution:** `scoped` keyword for automatic cleanup
```c
void handler(Request* req) {
    scoped resp = new Response();     // auto defer delete
    scoped data = new List<Item*>();
    scoped cache = new Map<String, int>();
    // all cleaned up automatically
}
```

**Benefit:** Reduces line count by 50% for memory-heavy functions

---

### 4. **Dict Arrays are Broken**

**Problems:**
```c
dict d = json("{\"items\":[{\"v\":7}]}");

// 1. Numeric leaf crashes
dict v = d.items[0].v;  // v = 0x7 (int cast to dict)
json(v);  // SIGSEGV: dereferences 0x7

// 2. Can't iterate
for (auto x in d.items)  // runs 0 iterations - silent failure

// 3. No length
int len = d.items.length;  // doesn't exist
```

**Solutions needed:**
1. Add `d.items.length` accessor
2. Fix `for-in` on dict arrays
3. Add type-safe accessors: `d.items.int_at(i)`, `d.items.str_at(i)`

---

### 5. **Exception + Defer Interaction Unclear**

**Problem:**
```c
try {
    List<int>* nums = new List<int>();
    // forgot defer delete nums
    
    throw(RuntimeException, "oops");
    
} catch (Exception e) {
    // Was nums cleaned up? Leaked?
}
```

**Solution:** Document and test the rule clearly
```c
// RULE: defer runs on ALL scope exits (return, throw, break)

try {
    auto x = new Foo();
    defer delete x;  // ✅ runs even if throw
    
    throw(...);
} catch (e) {
    // x already deleted here
}
```

**Status:** Add `cy-validate/val-017-exception-defer.cy` test

---

## Medium Priority Issues

### 6. **Filter/Map/Slice Return Heap Allocations**

Easy to leak intermediates:
```c
auto result = nums->Filter(pred)->Map(fn)->Slice(0, 10);
// 3 heap allocations, only result is deferred - LEAK 2!
```

### 7. **Track* vs Track Confusion**

Users unsure when to use `List<Track>` vs `List<Track*>` and what each means for ownership.

**Solution:** Better documentation + the `List.owns()` feature from #1

### 8. **No Compile Warning for Missing defer**

```c
List<int>* nums = new List<int>();
// ... no defer delete in scope ...
// Silent leak, no warning
```

**Solution:** Static analyzer pass to warn on un-deferred heap allocations

---

## What Doesn't Need Fixing

1. **`__destroy` works perfectly** - Tested in `val-015-collection-byval-dtor.cy`
2. **Automatic String arena** - Tested in `val-002-string-arena.cy` (200k alloc loop stays bounded)
3. **`defer` on all exit paths** - Works correctly with return/throw/break
4. **Generic type safety** - No casts needed, full compile-time checking

---

## Implementation Priority

### High Priority (fixes critical footguns)
1. ✅ **`is_pointer<T>` intrinsic** - Design complete (see COMPILER_INTRINSIC_is_pointer.md)
2. Dict array fixes (length, for-in, type-safe access)
3. Exception + defer test coverage

### Medium Priority (reduces boilerplate)
4. `scoped` keyword for automatic defer delete
5. Lint warnings for missing defer / detached strings
6. Better docs for Track* vs Track

### Low Priority (nice-to-have)
7. OwnedString type for explicit ownership
8. Intermediate allocation tracking for chains
9. Stack-allocated collections with inline storage

---

## See Also

- `COMPILER_INTRINSIC_is_pointer.md` - Full spec for ownership-aware collections
- `SHORTCOMINGS.md` - Known language limitations
- `GENERICSMEM.md` - How generics + memory management work
- `cy-validate/val-015-collection-byval-dtor.cy` - __destroy test
- `examples