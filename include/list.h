/* list.h — Generic dynamic-array collection for ClassyC
 *
 * Provides a production-ready List<T> with 30 methods covering:
 *   · Constructors (default, capacity, singleton, array-view)
 *   · Accessors    (Count, Capacity, IsEmpty, Get, First, Last)
 *   · Capacity     (EnsureCapacity, TrimExcess)
 *   · Mutation     (Set, Add, Insert, Pop, RemoveAt, Remove, Clear)
 *   · Search       (IndexOf, LastIndexOf, Contains)
 *   · Transform    (Reverse, Sort, Concat, Slice, Copy, Equals)
 *   · Higher-order (ForEach, Filter)
 *
 * Usage:
 *   #include "list.h"
 *   List<int>* nums = new List<int>();
 *   nums->Add(42);
 *   defer delete nums;
 *
 *   // from a C array, explicit array-view constructor:
 *   String arr[3] = {"a", "b", "c"};
 *   List<String>* lst = new List<String>(arr, 3);
 *
 *   // ...or let the compiler supply the count via arr.ToList(), which lowers
 *   // to the same List(T* items, int count) constructor below:
 *   List<String>* lst2 = arr.ToList();
 *
 * Memory: Caller owns heap-allocated List<T> instances. Use `defer delete` for
 * scope-bound cleanup. Slice() and Copy() return new heap allocations that the
 * caller must also free. Filter() similarly returns a new heap list.
 *
 * Thread safety: None. External synchronization required for shared access.
 */

#ifndef CLASSYC_LIST_H
#define CLASSYC_LIST_H

#include <stdlib.h>

class List<T> {
    T*  data;
    int length;
    int capacity;

    /* ═══════════════════════════ Constructors ═══════════════════════════ */

    /* Default: empty list with initial capacity of 4. */
    List() {
        this->length   = 0;
        this->capacity = 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
    }

    /* Pre-sized: empty list with the given initial capacity.
     * Clamps non-positive values to the default capacity of 4. */
    List(int initialCapacity) {
        this->length   = 0;
        this->capacity = initialCapacity > 0 ? initialCapacity : 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
    }

    /* Singleton: list pre-loaded with one element.
     * For T=int, the capacity constructor wins tie-breaking on integer literals. */
    List(T firstItem) {
        this->length   = 0;
        this->capacity = 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        this->data[0]  = firstItem;
        this->length   = 1;
    }

    /* Array view: copy the elements of a plain C array (or slice).
     * Caller keeps ownership of the source.  This is also the constructor that
     * `arr.ToList()` and `new List<T>(arr)` lower to.  `items` is a bare T*
     * which carries no length of its own, so the element count is recovered via
     * `items.count()`: the compiler threads the source array's (statically
     * known) / slice's length alongside the pointer into a hidden companion. */
    List(T* items) {
        int n = items.count();
        this->length   = 0;
        this->capacity = n > 0 ? n : 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        for (int i = 0; i < n; i++) {
            this->data[i] = items[i];
            this->length++;
        }
    }

    /* ═══════════════════════════ Destructor ═════════════════════════════════ */

    ~List() {
        if (this->data) free((void*) this->data);
    }

    /* ═══════════════════════════ Accessors ═════════════════════════════ */

    int Count()    { return this->length; }
    int Capacity() { return this->capacity; }
    int IsEmpty()  { return this->length == 0; }

    /* Indexed access. Caller must ensure 0 <= index < Count(). */
    T Get(int index) { return this->data[index]; }

    /* Convenience: first/last element. Undefined behavior on empty list. */
    T First() { return this->data[0]; }
    T Last()  { return this->data[this->length - 1]; }

    /* ═════════════════════════ Capacity management ══════════════════════ */

    /* Ensure capacity >= min. Doubles until satisfied; preserves elements. */
    void EnsureCapacity(int min) {
        if (min <= this->capacity) return;
        int newCap = this->capacity;
        while (newCap < min) newCap = newCap * 2;
        T* newData = (T*) malloc(sizeof(T) * newCap);
        for (int i = 0; i < this->length; i++) newData[i] = this->data[i];
        free((void*) this->data);
        this->data     = newData;
        this->capacity = newCap;
    }

    /* Shrink backing array to Count() (minimum 1). Releases wasted memory. */
    void TrimExcess() {
        int target = this->length > 0 ? this->length : 1;
        if (target == this->capacity) return;
        T* newData = (T*) malloc(sizeof(T) * target);
        for (int i = 0; i < this->length; i++) newData[i] = this->data[i];
        free((void*) this->data);
        this->data     = newData;
        this->capacity = target;
    }

    /* ═════════════════════════ Search ══════════════════════════════════ */

    /* Index of first element equal to item via ==, or -1. */
    int IndexOf(T item) {
        for (int i = 0; i < this->length; i++)
            if (this->data[i] == item) return i;
        return -1;
    }

    /* Index of last element equal to item via ==, or -1. */
    int LastIndexOf(T item) {
        int i = this->length - 1;
        while (i >= 0) {
            if (this->data[i] == item) return i;
            i--;
        }
        return -1;
    }

    /* Returns 1 if any element equals item via ==, 0 otherwise. */
    int Contains(T item) { return this->IndexOf(item) >= 0; }

    /* ═══════════════════════════ Mutation ═════════════════════════====== */

    /* Replace element at index. No-op if out of range. */
    void Set(int index, T item) {
        if (index >= 0 && index < this->length) this->data[index] = item;
    }

    /* Append to end. Grows capacity as needed. */
    void Add(T item) {
        this->EnsureCapacity(this->length + 1);
        this->data[this->length] = item;
        this->length++;
    }

    /* Insert before index (clamped). Later elements shift right. */
    void Insert(int index, T item) {
        if (index < 0)           index = 0;
        if (index > this->length) index = this->length;
        this->EnsureCapacity(this->length + 1);
        for (int i = this->length; i > index; i--) this->data[i] = this->data[i - 1];
        this->data[index] = item;
        this->length++;
    }

    /* Remove and return last element. Undefined on empty list. */
    T Pop() {
        this->length--;
        return this->data[this->length];
    }

    /* Remove by index, shift left. No-op if out of range. */
    void RemoveAt(int index) {
        if (index < 0 || index >= this->length) return;
        for (int i = index; i < this->length - 1; i++)
            this->data[i] = this->data[i + 1];
        this->length--;
    }

    /* Remove first occurrence of item via ==. Returns 1 on success, 0 if absent. */
    int Remove(T item) {
        int idx = this->IndexOf(item);
        if (idx < 0) return 0;
        this->RemoveAt(idx);
        return 1;
    }

    /* Clear all elements. Retains backing capacity for reuse. */
    void Clear() { this->length = 0; }

    /* ═════════════════════════ Transformations ═════════════════════════ */

    /* Reverse elements in place. O(n). */
    void Reverse() {
        int lo = 0, hi = this->length - 1;
        while (lo < hi) {
            T tmp = this->data[lo];
            this->data[lo] = this->data[hi];
            this->data[hi] = tmp;
            lo++;
            hi--;
        }
    }

    /* Sort in place using Shell sort (O(n log² n) average).
     * cmp(a, b) returns <0 if a<b, 0 if equal, >0 if a>b. */
    void Sort(int(*cmp)(T, T)) {
        int gap = this->length / 2;
        while (gap > 0) {
            for (int i = gap; i < this->length; i++) {
                T   tmp = this->data[i];
                int j   = i;
                while (j >= gap && cmp(this->data[j - gap], tmp) > 0) {
                    this->data[j] = this->data[j - gap];
                    j = j - gap;
                }
                this->data[j] = tmp;
            }
            gap = gap / 2;
        }
    }

    /* Append all elements of `other` to this list. Returns this for chaining. */
    List<T>* Concat(List<T>* other) {
        for (auto item in other) this->Add(item);
        return this;
    }

    /* Return new heap list with [start, start+count). Clamps to valid range.
     * Caller must `delete` the result. */
    List<T>* Slice(int start, int count) {
        if (start < 0)                   start = 0;
        if (start >= this->length)       count = 0;
        if (count < 0)                   count = 0;
        if (start + count > this->length) count = this->length - start;
        List<T>* result = new List<T>(count > 0 ? count : 1);
        for (int i = 0; i < count; i++) result->Add(this->data[start + i]);
        return result;
    }

    /* Return shallow copy. Caller must `delete`. */
    List<T>* Copy() {
        List<T>* c = new List<T>(this->length > 0 ? this->length : 1);
        for (int i = 0; i < this->length; i++)
            c->Add(this->data[i]);
        return c;
    }

    /* Structural equality: same count + pairwise ==. Returns 1 or 0. */
    int Equals(List<T>* other) {
        if (other == NULL || other->Count() != this->length) return 0;
        for (int i = 0; i < this->length; i++)
            if (this->data[i] != other->Get(i)) return 0;
        return 1;
    }

    /* ═════════════════════════ Higher-order ═══════════════════════════ */

    /* Call action(item) for each element in order. */
    void ForEach(void(*action)(T)) {
        for (int i = 0; i < this->length; i++)
            action(this->data[i]);
    }

    /* Return new heap list containing elements where pred(item) != 0.
     * Caller must `delete`. */
    List<T>* Filter(int(*pred)(T)) {
        List<T>* result = new List<T>();
        for (int i = 0; i < this->length; i++)
            if (pred(this->data[i])) result->Add(this->data[i]);
        return result;
    }
};

#endif /* CLASSYC_LIST_H */
