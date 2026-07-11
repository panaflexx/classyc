/* set.h — Generic high-performance hash set for ClassyC
 *
 * Production-ready Set<T> with open-addressing + triangular probing,
 * power-of-two sizing, load factor <= 0.7, tombstone deletion, and a
 * dense live-element backing array for O(1) indexed access and cheap
 * iteration compatible with `for (auto x in set)`.
 *
 * Performance (average, good distribution):
 *   Add / Remove / Contains : O(1)
 *   Iteration (Count/Get)   : O(1) per element
 *
 * Hashing & equality strategy:
 *   ClassyC's `==` on String is *pointer* identity, not content equality, and
 *   string literals are not interned.  A hash set therefore needs content-based
 *   hashing AND content-based comparison for String, while every other type is
 *   hashed/compared by its raw value bytes.  We pick the right pair at compile
 *   time with C11 `_Generic`: the associations select between two helper
 *   functions that share one signature, so the single call site type-checks for
 *   every specialization T (int, double, pointers, String, small PODs, ...).
 *
 * Usage:
 *   #include "set.h"
 *   Set<int>* s = new Set<int>{1, 2, 3};
 *   s->Add(42);
 *   if (s->Contains(42)) ...
 *   for (auto v in s) printf("%d\n", v);
 *   defer delete s;
 *
 * Memory: Caller owns via `defer delete`. Union/Intersect/Difference/Filter
 * return new heap sets the caller must free.
 *
 * Thread safety: None.
 */

#ifndef CLASSYC_SET_H
#define CLASSYC_SET_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ───────────────────────────── Hash helpers ───────────────────────────── */

static inline uint64_t set_fnv1a(const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* The two hash/equality helpers in each pair share one signature so that
 * `_Generic(key, String: strkey, default: bytes)(&key, ...)` type-checks no
 * matter which concrete type `key` ends up being after specialization. */

static inline uint64_t set_hash_bytes(const void* keyp, size_t n) {
    return set_fnv1a(keyp, n);
}

static inline uint64_t set_hash_strkey(const void* keyp, size_t n) {
    (void)n;
    const char* s = *(const char* const*)keyp;   /* keyp points at a String */
    return s != NULL ? set_fnv1a(s, strlen(s)) : 0;
}

static inline int set_eq_bytes(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n) == 0;
}

static inline int set_eq_strkey(const void* a, const void* b, size_t n) {
    (void)n;
    const char* x = *(const char* const*)a;
    const char* y = *(const char* const*)b;
    if (x == y)               return 1;
    if (x == NULL || y == NULL) return 0;
    return strcmp(x, y) == 0;
}

/* Content-aware hash / equality for a key of any specialization type. */
#define SET_HASH(k)   (_Generic((k), String: set_hash_strkey, default: set_hash_bytes)(&(k), sizeof(k)))
#define SET_EQ(a, b)  (_Generic((a), String: set_eq_strkey,   default: set_eq_bytes)(&(a), &(b), sizeof(a)))

/* ───────────────────────────── Set<T> ───────────────────────────── */

class Set<T> {
    /* Dense array of live elements (for O(1) Get + for-in) */
    T*   dense;
    int  count;
    int  capacity;

    /* Hash table: stores indices into dense[], or -1 (empty) / -2 (tombstone) */
    int* table;
    int  table_cap;   /* always a power of two */
    int  used;        /* live + tombstones */
    int  _owns_ptrs;  /* ownership flag: 1 = delete pointer elements on dtor */

    /* ───────────────────── Internal helpers ───────────────────── */

    /* Find the table slot for `key`: either the slot already holding an equal
     * element, or the first empty slot at which it would be inserted. The table
     * never reaches 100% occupancy, so an empty (-1) slot always exists and the
     * probe loop terminates. */
    int find_slot(T key) const {
        uint64_t h = SET_HASH(key);
        int mask = this->table_cap - 1;
        int i = (int)(h & (uint64_t)mask);
        int step = 1;
        for (;;) {
            int idx = this->table[i];
            if (idx == -1) return i;                          /* empty */
            if (idx >= 0 && SET_EQ(this->dense[idx], key)) return i; /* found */
            i = (i + step) & mask;                            /* triangular probe */
            step++;
        }
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
                int slot = this->find_slot(this->dense[idx]);
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
        this->dense    = (T*)malloc(sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;

        this->table_cap = 16;
        while (this->table_cap < this->capacity * 2) this->table_cap *= 2;
        this->table = (int*)malloc(sizeof(int) * this->table_cap);
        for (int i = 0; i < this->table_cap; i++) this->table[i] = -1;
        this->used = 0;
    }

    /* ───────────────────── Constructors ───────────────────── */

    Set() {
        this->init_storage(4);
    }

    Set(int initialCapacity) {
        this->init_storage(initialCapacity);
    }

    /* Destroy each live element, then release the backing buffers.
     *
     * __destroy(x) is a compiler intrinsic: for a by-value class element type
     * with a destructor it runs that destructor on x; for scalars, String, and
     * pointer element types it expands to nothing.  This is what makes
     * `delete set` (or a Set on a `defer delete`) reclaim its by-value class
     * elements — owned storage dies with the owner.
     *
     * is_pointer<T>() is a compiler intrinsic that returns 1 if T is a pointer
     * type. Combined with the _owns_ptrs flag (set via owns()), this deletes
     * owned pointer elements automatically. */
    ~Set() {
        for (int i = 0; i < this->count; i++) {
            if (this->_owns_ptrs && is_pointer<T>()) {
                delete this->dense[i];  /* delete owned pointer elements */
            } else {
                __destroy(this->dense[i]);  /* by-value or non-owned */
            }
        }
        if (this->dense) free((void*)this->dense);
        if (this->table) free((void*)this->table);
    }

    /* ───────────────────── Core API ───────────────────── */

    int  Count()    const { return this->count; }
    int  Capacity() const { return this->capacity; }
    int  IsEmpty()  const { return this->count == 0; }

    /* owns(): mark this set as the owner of its pointer elements.
     * Usage: Set<Track*>* s = new Set<Track*>().owns();
     * When the set is deleted, it also deletes each T* element. Returns this. */
    Set<T>* owns() {
        this->_owns_ptrs = 1;
        return this;
    }

    int Contains(T item) const {
        if (this->table_cap == 0) return 0;
        int slot = this->find_slot(item);
        return this->table[slot] >= 0 ? 1 : 0;
    }

    /* Returns 1 if newly inserted, 0 if already present. */
    int Add(T item) {
        this->ensure_table();

        int slot = this->find_slot(item);
        if (this->table[slot] >= 0) return 0;   /* duplicate */

        if (this->count == this->capacity) {
            this->capacity *= 2;
            this->dense = (T*)realloc((void*)this->dense, sizeof(T) * this->capacity);
        }
        int idx = this->count;
        this->dense[idx] = item;
        this->count++;

        this->table[slot] = idx;
        this->used++;
        return 1;
    }

    /* Array-view constructor (also used by `new Set<T>(arr)` and `arr.ToSet()`):
     * `items` is a bare T* whose element count is recovered via items.count().
     * Defined here, after Add(), because a method may only call methods that
     * are declared earlier in the class body. */
    Set(T* items) {
        int n = items.count();
        this->init_storage(n > 0 ? n : 4);
        for (int i = 0; i < n; i++) this->Add(items[i]);
    }

    /* Returns 1 if removed, 0 if not found. */
    int Remove(T item) {
        if (this->table_cap == 0) return 0;
        int slot = this->find_slot(item);
        int idx = this->table[slot];
        if (idx < 0) return 0;

        /* Swap-remove from dense and repoint the moved element's slot. */
        int last = this->count - 1;
        if (idx != last) {
            this->dense[idx] = this->dense[last];
            int slot2 = this->find_slot(this->dense[last]);
            this->table[slot2] = idx;
        }
        this->count--;

        this->table[slot] = -2;   /* tombstone */
        return 1;
    }

    T Get(int index) const { return this->dense[index]; }
    T First() const { return this->dense[0]; }
    T Last()  const { return this->dense[this->count - 1]; }

    void Clear() {
        this->count = 0;
        this->used  = 0;
        for (int i = 0; i < this->table_cap; i++) this->table[i] = -1;
    }

    /* ───────────────────── Set operations ───────────────────── */

    Set<T> Union(Set<T>* other) const {
        auto r = Set<T>(this->count + other->Count());
        for (int i = 0; i < this->count; i++)        r.Add(this->dense[i]);
        for (int i = 0; i < other->Count(); i++)     r.Add(other->Get(i));
        return move r;
    }

    Set<T> Intersect(Set<T>* other) const {
        auto r = Set<T>(this->count);
        for (int i = 0; i < this->count; i++) {
            T v = this->dense[i];
            if (other->Contains(v)) r.Add(v);
        }
        return move r;
    }

    Set<T> Difference(Set<T>* other) const {
        auto r = Set<T>(this->count);
        for (int i = 0; i < this->count; i++) {
            T v = this->dense[i];
            if (!other->Contains(v)) r.Add(v);
        }
        return move r;
    }

    int IsSubsetOf(Set<T>* other) const {
        if (this->count > other->Count()) return 0;
        for (int i = 0; i < this->count; i++)
            if (!other->Contains(this->dense[i])) return 0;
        return 1;
    }

    int Equals(Set<T>* other) const {
        if (this->count != other->Count()) return 0;
        for (int i = 0; i < this->count; i++)
            if (!other->Contains(this->dense[i])) return 0;
        return 1;
    }

    /* ───────────────────── Higher-order ───────────────────── */

    void ForEach(void(*action)(T)) const __attribute__((da_ignore)) {
        for (int i = 0; i < this->count; i++) action(this->dense[i]);
    }

    Set<T> Filter(int(*pred)(T)) const __attribute__((da_ignore)) {
        auto r = Set<T>(this->count);
        for (int i = 0; i < this->count; i++)
            if (pred(this->dense[i])) r.Add(this->dense[i]);
        return move r;
    }

};

#endif /* CLASSYC_SET_H */
