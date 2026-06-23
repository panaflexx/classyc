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
 *   Map<String, int>* ages = new Map<String, int>();
 *   ages->Set("Ada", 36);
 *   ages["Ada"] = 37;                       // subscript write == Set(key, val)
 *   int a = ages["Ada"];                    // subscript read  == Get(key)
 *   if (ages->Contains("Ada")) ...
 *   for (auto name, age in ages) ...        // (key, value) iteration
 *   defer delete ages;
 *
 *   // string -> object mapping:
 *   Map<String, Track*>* lib = new Map<String, Track*>();
 *   lib->Set("Kashmir", new Track("Kashmir", 508));
 *
 * Subscript & iteration sugar:
 *   map[k]        -> map->Get(k)          (read)
 *   map[k] = v    -> map->Set(k, v)       (write / insert / update)
 *   for (auto k in map)        -> k over keys, in insertion order
 *   for (auto k, v in map)     -> k = key, v = value
 *
 * Missing keys: Get(k) on an absent key returns a zero-initialized V (0 / NULL /
 * empty).  Use Contains(k) to distinguish "absent" from "present with zero
 * value", or GetOr(k, fallback) to supply your own default.
 *
 * Memory: Caller owns via `defer delete`.  Copy()/Merge() return new heap maps
 * the caller must free.  For Map<…, MyClass*> the map owns only the pointers,
 * not the pointed-to objects (free those yourself).
 *
 * Thread safety: None.
 */

#ifndef CLASSYC_MAP_H
#define CLASSYC_MAP_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

    /* ───────────────────── Internal helpers ───────────────────── */

    /* Find the table slot for `key`: either the slot already holding an equal
     * key, or the first empty slot at which it would be inserted.  The table
     * never reaches 100% occupancy, so an empty (-1) slot always exists and the
     * probe loop terminates. */
    int find_slot(K key) const {
        uint64_t h = MAP_HASH(key);
        int mask = this->table_cap - 1;
        int i = (int)(h & (uint64_t)mask);
        int step = 1;
        for (;;) {
            int idx = this->table[i];
            if (idx == -1) return i;                                /* empty */
            if (idx >= 0 && MAP_EQ(this->keys[idx], key)) return i; /* found */
            i = (i + step) & mask;                                  /* triangular probe */
            step++;
        }
    }

    /* Dense index of `key`, or -1 if absent. */
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
                int slot = this->find_slot(this->keys[idx]);
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

    void init_storage(int cap) {
        this->count    = 0;
        this->capacity = cap > 4 ? cap : 4;
        this->keys     = (K*)malloc(sizeof(K) * this->capacity);
        this->vals     = (V*)malloc(sizeof(V) * this->capacity);

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
     * keys/values — owned storage dies with the owner. */
    ~Map() {
        for (int i = 0; i < this->count; i++) {
            __destroy(this->keys[i]);
            __destroy(this->vals[i]);
        }
        if (this->keys)  free((void*)this->keys);
        if (this->vals)  free((void*)this->vals);
        if (this->table) free((void*)this->table);
    }

    /* ───────────────────── Accessors ───────────────────── */

    int Count()    const { return this->count; }
    int Capacity() const { return this->capacity; }
    int IsEmpty()  const { return this->count == 0; }

    int Contains(K key) const { return this->find_index(key) >= 0 ? 1 : 0; }

    /* Value for `key`, or a zero-initialized V if absent (subscript read). */
    V Get(K key) const {
        int idx = this->find_index(key);
        if (idx >= 0) return this->vals[idx];
        V zero;
        memset((void*)&zero, 0, sizeof(V));
        return zero;
    }

    /* Value for `key`, or `fallback` if absent. */
    V GetOr(K key, V fallback) const {
        int idx = this->find_index(key);
        if (idx >= 0) return this->vals[idx];
        return fallback;
    }

    /* Insertion-ordered indexed access (powers for-in and KeyAt/ValAt loops).
     * Caller must ensure 0 <= index < Count(). */
    K KeyAt(int index) const { return this->keys[index]; }
    V ValAt(int index) const { return this->vals[index]; }

    /* ───────────────────── Mutation ───────────────────── */

    /* Insert or update `key` -> `val`.  Returns 1 if a new key was inserted,
     * 0 if an existing key's value was overwritten.  (Subscript write.) */
    int Set(K key, V val) {
        this->ensure_table();

        int slot = this->find_slot(key);
        int idx  = this->table[slot];
        if (idx >= 0) {                 /* existing key: overwrite value */
            this->vals[idx] = val;
            return 0;
        }

        if (this->count == this->capacity) {
            this->capacity *= 2;
            this->keys = (K*)realloc((void*)this->keys, sizeof(K) * this->capacity);
            this->vals = (V*)realloc((void*)this->vals, sizeof(V) * this->capacity);
        }
        int n = this->count;
        this->keys[n] = key;
        this->vals[n] = val;
        this->count++;

        this->table[slot] = n;
        this->used++;
        return 1;
    }

    /* Remove `key`.  Returns 1 if removed, 0 if not present. */
    int Remove(K key) {
        if (this->table_cap == 0) return 0;
        int slot = this->find_slot(key);
        int idx  = this->table[slot];
        if (idx < 0) return 0;

        /* Swap-remove from the dense arrays and repoint the moved entry's slot. */
        int last = this->count - 1;
        if (idx != last) {
            this->keys[idx] = this->keys[last];
            this->vals[idx] = this->vals[last];
            int slot2 = this->find_slot(this->keys[last]);
            this->table[slot2] = idx;
        }
        this->count--;

        this->table[slot] = -2;   /* tombstone */
        return 1;
    }

    void Clear() {
        this->count = 0;
        this->used  = 0;
        for (int i = 0; i < this->table_cap; i++) this->table[i] = -1;
    }

    /* ───────────────────── Bulk / functional ───────────────────── */

    /* Copy all entries of `other` into this map (overwriting on key clash).
     * Returns this for chaining. */
    Map<K, V>* Merge(Map<K, V>* other) {
        for (int i = 0; i < other->Count(); i++)
            this->Set(other->KeyAt(i), other->ValAt(i));
        return this;
    }

    /* Return a new heap map with the same entries.  Caller must `delete`. */
    Map<K, V>* Copy() const {
        Map<K, V>* r = new Map<K, V>(this->count > 0 ? this->count : 4);
        for (int i = 0; i < this->count; i++)
            r->Set(this->keys[i], this->vals[i]);
        return r;
    }

    /* Call action(key, value) for each entry, in insertion order. */
    void ForEach(void(*action)(K, V)) const {
        for (int i = 0; i < this->count; i++)
            action(this->keys[i], this->vals[i]);
    }
};

#endif /* CLASSYC_MAP_H */
