/* map.h — Generic high-performance hash map (dictionary) for ClassyC
 *
 * Production-ready Map<K, V> with open-addressing + triangular probing,
 * power-of-two sizing, load factor <= 0.7, tombstone deletion, and parallel
 * dense backing arrays for keys and values.  The dense arrays give O(1)
 * indexed access (KeyAt / ValAt) and cheap insertion-ordered iteration, while
 * the index table gives O(1) keyed lookup / update / removal.
 *
 * This is the typed, type-safe sibling of the built-in heterogeneous `dict`:
 * a `dict` mixes value kinds and is keyed by strings, whereas `Map<K, V>` fixes
 * the key and value types at compile time, stores values inline (no boxing),
 * and works for any key type.
 *
 * Performance (average, good distribution):
 *   Set / Get / Contains / Remove : O(1)
 *   KeyAt / ValAt (indexed)       : O(1)
 *
 * Hashing & equality strategy (same approach as set.h):
 *   ClassyC's `==` on String is *pointer* identity, not content equality, and
 *   string literals are not interned.  A hash map therefore needs content-based
 *   hashing AND comparison for String keys, while every other key type is
 *   hashed/compared by its raw value bytes.  We pick the right pair at compile
 *   time with C11 `_Generic`: each association selects between two helpers that
 *   share one signature, so the single call site type-checks for every key
 *   specialization K (int, double, pointers, String, small PODs, ...).
 *
 *     Map<String, V>  -> keys hashed/compared by *content* (FNV-1a / strcmp)
 *     Map<int, V>, …  -> keys hashed/compared by *value bytes*
 *     Map<MyClass*, V>-> keys hashed/compared by *identity* (pointer bits)
 *
 * Usage:
 *   #include "map.h"
 *
 *   // Stack / value form (preferred locals — ~Map at scope exit):
 *   auto ages = Map<String, int>();
 *   // or:  Map<String, int> ages;  ages = Map... is move-only, use move
 *   ages.Set("Ada", 36);
 *   ages["Ada"] = 37;                       // works on stack Map values
 *   int a = ages["Ada"];
 *
 *   // Heap form when you need a pointer / owned binding:
 *   owned auto heap = new Map<String, int>();
 *   heap["Ada"] = 37;
 *
 *   // string -> object mapping:
 *   auto lib = Map<String, Track*>();
 *   lib.Set("Kashmir", new Track("Kashmir", 508));
 *
 * Subscript & iteration sugar (value or pointer receiver):
 *   map[k]        -> map.Get(k) / map->Get(k)
 *   map[k] = v    -> map.Set(k, v)
 *   for (auto k in map)        -> k over keys, in insertion order
 *   for (auto k, v in map)     -> k = key, v = value
 *     K and V may be by-value classes (same as List.Get for-in); loop vars are
 *     stack slots filled each iteration.  Mutate storage with GetMut/ValMut.
 *
 * Missing keys: Get(k) **throws KeyException**.  Prefer GetOr(k, fallback),
 * TryGet(k, &out), or try/catch when absence is expected.  Contains(k) /
 * ContainsKey(k) test presence without throwing.
 *
 * Memory: Stack Maps own their table; destructor runs at scope exit.
 * Heap Maps: caller owns (owned auto / defer delete / delete).
 * Where / SelectValues / SelectKeys / Copy return new heap maps the caller
 * must free (always non-owning of pointees -- does not copy ownsKeys/ownsValues).
 * Keys()/Values() return new List* the caller must free.
 * For Map of MyClass* the map owns only the pointers after ownsValues()/ownsKeys().
 * Move-only: bare assign of Map is an error; use move to transfer ownership.
 *
 * List/Map GroupBy results own the List* bucket values automatically
 * (ownsValues). Element ownership inside each bucket is still non-owning.
 * Free form `GroupBy(list, keyFn)` / UFCS `list->GroupBy(keyFn)` / ListGroupBy
 * live here to avoid a list.h↔map.h include cycle.
 *
 * Thread safety: None.
 */

#ifndef CLASSYC_MAP_H
#define CLASSYC_MAP_H

#include <stdlib.h>
#include <stdio.h>   /* snprintf for int-key ToDict */
#include <string.h>
#include <stdint.h>
#include "list.h"   /* List<T> for Keys()/Values(); shared dict_* declarations
                      and the `dict` type for ToDict()/ToJson() */

/* ───────────────────────────── Hash helpers ───────────────────────────── */

static inline uint64_t map_fnv1a(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* The two helpers in each pair share one signature so that
 * `_Generic(key, String: strkey, default: bytes)(&key, ...)` type-checks no
 * matter which concrete type `key` ends up being after specialization. */

static inline uint64_t map_hash_bytes(const void* keyp, size_t n) {
    return map_fnv1a(keyp, n);
}

static inline uint64_t map_hash_strkey(const void* keyp, size_t n) {
    (void)n;
    const char* s = *(const char* const*)keyp;   /* keyp points at a String */
    return s != NULL ? map_fnv1a(s, strlen(s)) : 0;
}

static inline int map_eq_bytes(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n) == 0;
}

static inline int map_eq_strkey(const void* a, const void* b, size_t n) {
    (void)n;
    const char* x = *(const char* const*)a;
    const char* y = *(const char* const*)b;
    if (x == y)                 return 1;
    if (x == NULL || y == NULL) return 0;
    return strcmp(x, y) == 0;
}

/* Content-aware hash / equality for a key of any specialization type. */
#define MAP_HASH(k)   (_Generic((k), String: map_hash_strkey, default: map_hash_bytes)(&(k), sizeof(k)))
#define MAP_EQ(a, b)  (_Generic((a), String: map_eq_strkey,   default: map_eq_bytes)(&(a), &(b), sizeof(a)))

/* ───────── Grouping<K,V>: key + bucket (result of GroupBy) ────────── */
class Grouping<K, V> {
    K key;
    List<V>* items;
    Grouping(K k) {
        this->key = k;
        this->items = new List<V>();
    }
    ~Grouping() {
        if (this->items) delete this->items;
    }
    K Key() { return this->key; }
    List<V>* Values() { return this->items; }
    int Count() { return this->items->Count(); }
};

/* ───────────────────────────── Map<K, V> ───────────────────────────── */

class Map<K, V> {
    /* Parallel dense arrays of live entries (insertion-ordered). */
    K*   keys;
    V*   vals;
    int  count;
    int  capacity;

    /* Hash table: stores indices into keys[]/vals[], or -1 (empty) / -2 (tombstone). */
    int* table;
    int  table_cap;   /* always a power of two */
    int  used;        /* live + tombstones */
    int  _owns_keys;  /* 1 = delete pointer keys on dtor */
    int  _owns_vals;  /* 1 = delete pointer values on dtor */

    /* ───────────────────── Internal helpers ───────────────────── */

    /* Find the table slot for `key`:
     *   · if the key is present, the slot whose table entry indexes it
     *   · otherwise the first empty slot (-1), or the first tombstone (-2)
     *     along the probe chain (so inserts can reuse tombstones)
     * Load factor is capped below 1, so a free/tomb slot always exists. */
    int find_slot(K key) const {
        uint64_t h = MAP_HASH(key);
        int mask = this->table_cap - 1;
        int i = (int)(h & (uint64_t)mask);
        int step = 1;
        int first_tomb = -1;
        for (;;) {
            int idx = this->table[i];
            if (idx == -1)
                return first_tomb >= 0 ? first_tomb : i;            /* free */
            if (idx == -2) {
                if (first_tomb < 0) first_tomb = i;                 /* reusable */
            } else if (MAP_EQ(*(this->keys + (idx)), key)) {
                return i;                                          /* found */
            }
            i = (i + step) & mask;                                  /* triangular probe */
            step++;
        }
    }

    /* Dense index of `key`, or a negative sentinel if absent (-1 empty / -2 tomb). */
    int find_index(K key) const {
        if (this->table_cap == 0) return -1;
        int slot = this->find_slot(key);
        return this->table[slot];
    }

    void grow_table() {
        int old_cap = this->table_cap;
        int* old = this->table;

        this->table_cap = old_cap ? old_cap * 2 : 16;
        this->table = (int*)malloc(sizeof(int) * this->table_cap);
        for (int i = 0; i < this->table_cap; i++) this->table[i] = -1;

        /* Rehash live entries only; tombstones evaporate. */
        this->used = 0;
        for (int i = 0; i < old_cap; i++) {
            int idx = old[i];
            if (idx >= 0) {
                int slot = this->find_slot(*(this->keys + (idx)));
                this->table[slot] = idx;
                this->used++;
            }
        }
        if (old) free((void*)old);
    }

    /* Keep the table below a 0.7 load factor (integer math: used+1 >= cap*0.7). */
    void ensure_table() {
        if (this->table_cap == 0 || (this->used + 1) * 10 >= this->table_cap * 7)
            this->grow_table();
    }

    /* Dense keys/vals use *(ptr + i), never ptr[i]: when V is List/Map/Set,
     * V* is a pointer-to-collection and vals[i] would lower to Get() sugar.
     * Capacity is zeroed so move-assign ~V on empty slots is a no-op. */
    void init_storage(int cap) {
        this->count    = 0;
        this->capacity = cap > 4 ? cap : 4;
        this->keys     = (K*)malloc(sizeof(K) * this->capacity);
        this->vals     = (V*)malloc(sizeof(V) * this->capacity);
        if (this->keys) memset((void*)this->keys, 0, sizeof(K) * this->capacity);
        if (this->vals) memset((void*)this->vals, 0, sizeof(V) * this->capacity);
        this->_owns_keys = 0;
        this->_owns_vals = 0;

        this->table_cap = 16;
        while (this->table_cap < this->capacity * 2) this->table_cap *= 2;
        this->table = (int*)malloc(sizeof(int) * this->table_cap);
        for (int i = 0; i < this->table_cap; i++) this->table[i] = -1;
        this->used = 0;
    }

    /* ───────────────────── Constructors / destructor ───────────────────── */

    Map() {
        this->init_storage(4);
    }

    Map(int initialCapacity) {
        this->init_storage(initialCapacity);
    }

    /* Destroy each live key and value, then release the backing buffers.
     *
     * __destroy(x) is a compiler intrinsic: for a by-value class element type
     * with a destructor it runs that destructor on x; for scalars, String, and
     * pointer element types it expands to nothing.  This is what makes
     * `delete map` (or a Map on a `defer delete`) reclaim its by-value class
     * keys/values — owned storage dies with the owner.
     *
     * is_pointer<K>() / is_pointer<V>() are compiler intrinsics returning 1 for
     * pointer types. Combined with the _owns_keys / _owns_vals flags (set via
     * ownsValues() / ownsKeys()), this deletes owned pointer keys/values too. */
    ~Map() {
        for (int i = 0; i < this->count; i++) {
            if (this->_owns_keys && is_pointer<K>()) delete *(this->keys + i);
            else                                     __destroy(*(this->keys + i));
            if (this->_owns_vals && is_pointer<V>()) delete *(this->vals + i);
            else                                     __destroy(*(this->vals + i));
        }
        if (this->keys)  free((void*)this->keys);
        if (this->vals)  free((void*)this->vals);
        if (this->table) free((void*)this->table);
    }

    /* ───────────────────── Accessors ───────────────────── */

    int Count()    const { return this->count; }
    int Capacity() const { return this->capacity; }
    int IsEmpty()  const { return this->count == 0; }

    /* ownsValues(): mark this map as owner of its pointer values.
     * Usage: Map<String, Track*>* m = new Map<String, Track*>().ownsValues();
     * When the map is deleted, it also deletes each V* value. Returns this. */
    Map<K, V>* ownsValues() {
        this->_owns_vals = 1;
        return this;
    }

    /* ownsKeys(): mark this map as owner of its pointer keys (rare; keys are
     * usually String/int). When deleted, it also deletes each K* key. Returns this. */
    Map<K, V>* ownsKeys() {
        this->_owns_keys = 1;
        return this;
    }

    /* owns(): own both keys and values. Returns this. */
    Map<K, V>* owns() {
        this->_owns_keys = 1;
        this->_owns_vals = 1;
        return this;
    }

    /* True if `key` is present.  C# Dictionary vocabulary. */
    bool Contains(K key) const { return this->find_index(key) >= 0; }

    /* Alias for Contains — same as C# ContainsKey. */
    bool ContainsKey(K key) const { return this->Contains(key); }

    /* Value for `key`.
     * throws KeyException if the key is absent.
     * Callers that want silent fallback should use GetOr() or TryGet(). */
    V Get(K key) const {
        int idx = this->find_index(key);
        if (idx >= 0) return *(this->vals + idx); /* Copy rewrite for move-only V */
        throw(KeyException, "missing key in Map");
        V zero;
        memset((void*)&zero, 0, sizeof(V));
        return zero;
    }

    /* Value for `key`, or `fallback` if absent. */
    V GetOr(K key, V fallback) const {
        int idx = this->find_index(key);
        if (idx >= 0) return *(this->vals + idx);
        return fallback;
    }

    /* Fast C++-style lookup.  Returns true and writes *out if present;
     * returns false (leaves *out untouched) if absent.  No exception.
     * Move-only V: storage assign rewrites to deep Copy(). */
    bool TryGet(K key, V* out) const {
        if (out == NULL) return false;
        int idx = this->find_index(key);
        if (idx >= 0) {
            *out = *(this->vals + idx);
            return true;
        }
        return false;
    }

    /* Insertion-ordered indexed access (powers for-in and KeyAt/ValAt loops).
     * Throws OutOfBoundsException if index is not in [0, Count()). */
    K KeyAt(int index) const {
        if (index < 0 || index >= this->count)
            throw(OutOfBoundsException, "Map.KeyAt oob");
        return *(this->keys + index);
    }
    V ValAt(int index) const {
        if (index < 0 || index >= this->count)
            throw(OutOfBoundsException, "Map.ValAt oob");
        return *(this->vals + index);
    }

    /* Pointer into the dense value array — mutate class values in place
     * (Get/ValAt return copies).  Invalidated by rehash/growth. */
    V* ValMut(int index) __attribute__((da_ignore)) {
        if (index < 0 || index >= this->count)
            throw(OutOfBoundsException, "Map.ValMut oob");
        return this->vals + index;
    }

    /* Lookup + mut pointer.  Throws KeyException if missing (same as Get) so
     * [] GetMut-lvalue never builds a MEM op from a null pointer. */
    V* GetMut(K key) __attribute__((da_ignore)) {
        int idx = this->find_index(key);
        if (idx < 0) throw(KeyException, "Map.GetMut missing key");
        return this->vals + idx;
    }

    /* Destroy one key slot (by-value dtor or owned pointer delete). */
    void destroy_key_at(int i) {
        if (this->_owns_keys && is_pointer<K>()) delete *(this->keys + i);
        else                                     __destroy(*(this->keys + i));
    }

    /* Destroy one value slot. */
    void destroy_val_at(int i) {
        if (this->_owns_vals && is_pointer<V>()) delete *(this->vals + i);
        else                                     __destroy(*(this->vals + i));
    }

    /* ───────────────────── Mutation ───────────────────── */

    /* Insert or update `key` -> `val`.  Returns 1 if a new key was inserted,
     * 0 if an existing key's value was overwritten.  On overwrite the old
     * value is destroyed (by-value dtor or owned pointer delete). */
    int Set(K key, V val) {
        this->ensure_table();

        int slot = this->find_slot(key);
        int idx  = this->table[slot];
        if (idx >= 0) {                 /* existing key: overwrite value */
            this->destroy_val_at(idx);
            *(this->vals + idx) = move val;
            return 0;
        }

        if (this->count == this->capacity) {
            int old_cap = this->capacity;
            this->capacity *= 2;
            this->keys = (K*)realloc((void*)this->keys, sizeof(K) * this->capacity);
            this->vals = (V*)realloc((void*)this->vals, sizeof(V) * this->capacity);
            if (this->keys)
                memset((void*)(this->keys + old_cap), 0, sizeof(K) * (this->capacity - old_cap));
            if (this->vals)
                memset((void*)(this->vals + old_cap), 0, sizeof(V) * (this->capacity - old_cap));
        }
        int n = this->count;
        *(this->keys + n) = key;
        *(this->vals + n) = move val;
        this->count++;

        /* idx is -1 (empty) or -2 (tombstone).  `used` counts live+tomb:
         * reclaiming a tomb does not change used; claiming empty does. */
        this->table[slot] = n;
        if (idx == -1) this->used++;
        return 1;
    }

    /* Insert only if `key` is absent.  Returns true if inserted, false if the
     * key was already present (existing value left unchanged). */
    bool TryAdd(K key, V val) {
        if (this->Contains(key)) return false;
        this->Set(key, val);
        return true;
    }

    /* Remove `key`.  Returns 1 if removed, 0 if not present.  Destroys the
     * removed key and value (by-value / .ownsKeys/.ownsValues). */
    int Remove(K key) {
        if (this->table_cap == 0) return 0;
        int slot = this->find_slot(key);
        int idx  = this->table[slot];
        if (idx < 0) return 0;

        this->destroy_key_at(idx);
        this->destroy_val_at(idx);

        /* Swap-remove from the dense arrays and repoint the moved entry's slot.
         *
         * IMPORTANT: after keys[idx] = keys[last], find_slot(moved_key) can hit
         * `slot` first (keys[idx] already equals the moved key) and return the
         * entry we're about to tombstone — losing the survivor.  Probe for the
         * dense index `last` instead of matching by key equality. */
        int last = this->count - 1;
        if (idx != last) {
            *(this->keys + (idx)) = move *(this->keys + (last));
            *(this->vals + (idx)) = move *(this->vals + (last));

            uint64_t h = MAP_HASH(*(this->keys + (idx)));
            int mask = this->table_cap - 1;
            int i = (int)(h & (uint64_t)mask);
            int step = 1;
            for (;;) {
                if (this->table[i] == last) {
                    this->table[i] = idx;
                    break;
                }
                i = (i + step) & mask;
                step++;
            }
        } else {
            memset((void*)(this->keys + last), 0, sizeof(K));
            memset((void*)(this->vals + last), 0, sizeof(V));
        }
        this->count--;

        this->table[slot] = -2;   /* tombstone (used already counted this slot) */
        return 1;
    }

    /* Drop all entries, destroying keys/values first. */
    void Clear() {
        for (int i = 0; i < this->count; i++) {
            this->destroy_key_at(i);
            this->destroy_val_at(i);
        }
        this->count = 0;
        this->used  = 0;
        for (int i = 0; i < this->table_cap; i++) this->table[i] = -1;
    }

    /* ───────────────────── Accessors (extended) ───────────────────── */
    V GetOrAdd(K key, V fallback) __attribute__((da_ignore)) {
        int idx = this->find_index(key);
        if (idx >= 0) return *(this->vals + (idx));
        this->Set(key, fallback);
        return fallback;
    }
    /* True if any value equals `val`.  String values compare by content (MAP_EQ),
     * matching key equality and C# Dictionary.ContainsValue for strings. */
    bool ContainsValue(V val) const __attribute__((da_ignore)) {
        for (int i = 0; i < this->count; i++)
            if (MAP_EQ(*(this->vals + (i)), val)) return true;
        return false;
    }
    int AddOrUpdate(K key, V val, V(*updater)(V)) __attribute__((da_ignore)) {
        int idx = this->find_index(key);
        if (idx >= 0) {
            /* ValAt deep-copies move-only V into the updater arg path. */
            V cur = this->ValAt(idx);
            V updated = updater(cur);
            this->destroy_val_at(idx);
            *(this->vals + (idx)) = move updated;
            return 0;
        }
        this->Set(key, val);
        return 1;
    }

    /* ───────────────────── Bulk / functional ───────────────────── */

    /* Copy all entries of `other` into this map (overwriting on key clash).
     * Returns this for chaining.  NULL `other` is a no-op. */
    Map<K, V>* Merge(Map<K, V>* other) {
        if (!other) return this;
        for (int i = 0; i < other->Count(); i++)
            this->Set(other->KeyAt(i), other->ValAt(i));
        return this;
    }

    /* Shallow copy into a by-value map. Does not copy ownsKeys/ownsValues. */
    Map<K, V> Copy() const {
        auto r = Map<K, V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            r.Set(*(this->keys + (i)), *(this->vals + (i)));
        return move r;
    }

    /* ── Higher-order: Where / Any / All ─── */
    Map<K, V> Where(int(*pred)(K, V)) const __attribute__((da_ignore)) {
        auto r = Map<K, V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            if (pred(*(this->keys + (i)), *(this->vals + (i))))
                r.Set(*(this->keys + (i)), *(this->vals + (i)));
        return move r;
    }
    Map<K, V> WhereKeys(int(*pred)(K)) const __attribute__((da_ignore)) {
        auto r = Map<K, V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            if (pred(*(this->keys + (i))))
                r.Set(*(this->keys + (i)), *(this->vals + (i)));
        return move r;
    }
    Map<K, V> WhereValues(int(*pred)(V)) const __attribute__((da_ignore)) {
        auto r = Map<K, V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            if (pred(*(this->vals + (i))))
                r.Set(*(this->keys + (i)), *(this->vals + (i)));
        return move r;
    }
    int Any(int(*pred)(K, V)) const __attribute__((da_ignore)) {
        for (int i = 0; i < this->count; i++)
            if (pred(*(this->keys + (i)), *(this->vals + (i)))) return 1;
        return 0;
    }
    int All(int(*pred)(K, V)) const __attribute__((da_ignore)) {
        for (int i = 0; i < this->count; i++)
            if (!pred(*(this->keys + (i)), *(this->vals + (i)))) return 0;
        return 1;
    }
    Map<K, W> SelectValues<W>(W(*fn)(K, V)) const __attribute__((da_ignore)) {
        auto r = Map<K, W>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            r.Set(*(this->keys + (i)), fn(*(this->keys + (i)), *(this->vals + (i))));
        return move r;
    }
    Map<G, V> SelectKeys<G>(G(*fn)(K, V)) const __attribute__((da_ignore)) {
        auto r = Map<G, V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            r.Set(fn(*(this->keys + (i)), *(this->vals + (i))), *(this->vals + (i)));
        return move r;
    }
    /* Group values by keySelector(k,v).  Returns Map<G, List<V>> by value —
     * nested List shells live in the map dense buffer (Phase B).  Get/ValAt
     * deep-Copy a bucket; mutate in place via GetMut/ValMut. */
    Map<G, List<V>> GroupBy<G>(G(*keySelector)(K, V)) const __attribute__((da_ignore)) {
        auto result = Map<G, List<V>>();
        for (int i = 0; i < this->count; i++) {
            G gk = keySelector(*(this->keys + (i)), *(this->vals + (i)));
            if (!result.Contains(gk)) {
                auto empty = List<V>();
                result.Set(gk, move empty);
            }
            result.GetMut(gk)->Add(*(this->vals + (i)));
        }
        return move result;
    }

    /* Call action(key, value) for each entry, in insertion order. */
    void ForEach(void(*action)(K, V)) const {
        for (int i = 0; i < this->count; i++)
            action(*(this->keys + (i)), *(this->vals + (i)));
    }

    /* ───────────────────── Conversions ───────────────────── */

    /* Collect keys/values into by-value Lists (RAII). */
    List<K> Keys() const {
        auto r = List<K>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++) r.Add(*(this->keys + (i)));
        return move r;
    }

    List<V> Values() const {
        auto r = List<V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++) r.Add(*(this->vals + (i)));
        return move r;
    }

    /* Serialize to a JSON object `dict`.  Keys are read as String (intended for
     * Map<String, V>); the value is dispatched automagically over V at compile
     * time via nameof<V>(): int/long/short -> number, double/float -> number,
     * String -> string, dict -> passthrough, anything else -> null.  Each value
     * is read through a typed pointer (`*(int*)&v`, `*(double*)&v`, ...) so the
     * generic body type-checks for every V and only the matched branch runs.
     *
     *   dict cfg = settings->ToDict();   // {"limit":10,"name":"prod"}
     */
    dict ToDict() const __attribute__((da_ignore)) {
        dict obj = dict_create_object();
        const char* kt = nameof<K>();
        int k_is_str = (strcmp(kt, "String") == 0 || strcmp(kt, "char") == 0);
        int k_is_int = (strcmp(kt, "int") == 0 || strcmp(kt, "short") == 0
                       || strcmp(kt, "long") == 0 || strcmp(kt, "unsigned") == 0
                       || strcmp(kt, "bool") == 0);
        if (!k_is_str && !k_is_int) return obj;
        const char* vt = nameof<V>();
        for (int i = 0; i < this->count; i++) {
            V* p = (this->vals + (i));
            dict dv;
            if (strcmp(vt, "String") == 0 || strcmp(vt, "char") == 0)
                dv = dict_create_string(*(char**)p);
            else if (strcmp(vt, "double") == 0)
                dv = dict_create_number(*(double*)p);
            else if (strcmp(vt, "float") == 0)
                dv = dict_create_number((double)*(float*)p);
            else if (strcmp(vt, "long") == 0)
                dv = dict_create_int64(*(long*)p);
            else if (strcmp(vt, "short") == 0)
                dv = dict_create_int64((long)*(short*)p);
            else if (strcmp(vt, "int") == 0 || strcmp(vt, "unsigned") == 0
                     || strcmp(vt, "bool") == 0)
                dv = dict_create_int64((long)*(int*)p);
            else if (strcmp(vt, "dict") == 0)
                dv = *(dict*)p;
            else
                dv = dict_create_null();
            const char* kstr;
            char keybuf[64];
            if (k_is_str) {
                kstr = *(char**)(this->keys + (i));
            } else {
                long kv = 0;
                if (strcmp(kt, "long") == 0) kv = *(long*)(this->keys + (i));
                else if (strcmp(kt, "short") == 0) kv = (long)*(short*)(this->keys + (i));
                else kv = (long)*(int*)(this->keys + (i));
                snprintf(keybuf, sizeof(keybuf), "%ld", kv);
                kstr = keybuf;
            }
            dict_object_set(obj, (char*)kstr, dv);
        }
        return obj;
    }

    /* Serialize to a JSON object String (e.g. {"a":1,"b":2}).  Builds a
     * transient dict via ToDict(), serializes it, then frees it.  Intended for
     * Map<String, scalar V>; for Map<String, dict> serialize ToDict() yourself
     * (ToJson() would free the referenced value dicts). */
    String ToJson() const {
        dict obj = this->ToDict();
        String j = obj.json();
        dict_destroy(obj);
        return j;
    }

    /* C# / f-string friendly aliases.  Must sit after ToJson() so the body can
     * call it (method order matters in ClassyC generics). */
    String ToString() const { return this->ToJson(); }
    String to_string() const { return this->ToJson(); }

};

/* List.GroupBy as a free generic function (method form via UFCS).
 * Returns Map<G, List<T>> by value (nested List shells, not List*).
 *   auto g = nums.GroupBy(parity);
 *   auto g = GroupBy(&nums, keyFn);
 *   auto g = ListGroupBy(&nums, keyFn);
 * Buckets: Get/ValAt → deep Copy(); mutate with GetMut/ValMut.
 */
Map<G, List<T>> GroupBy<T, G>(List<T>* self, G(*keySelector)(T))
    __attribute__((da_ignore)) {
    auto result = Map<G, List<T>>();
    if (self) {
        for (int i = 0; i < self->Count(); i++) {
            T item = self->Get(i);
            G gk = keySelector(item);
            if (!result.Contains(gk)) {
                auto empty = List<T>();
                result.Set(gk, move empty);
            }
            result.GetMut(gk)->Add(item);
        }
    }
    return move result;
}

/* Compat alias of GroupBy (pre-UFCS name). */
Map<G, List<T>> ListGroupBy<T, G>(List<T>* self, G(*keySelector)(T))
    __attribute__((da_ignore)) {
    auto result = Map<G, List<T>>();
    if (self) {
        for (int i = 0; i < self->Count(); i++) {
            T item = self->Get(i);
            G gk = keySelector(item);
            if (!result.Contains(gk)) {
                auto empty = List<T>();
                result.Set(gk, move empty);
            }
            result.GetMut(gk)->Add(item);
        }
    }
    return move result;
}

#endif /* CLASSYC_MAP_H */
