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
* Pointer ownership:
*   - List<T> (by-value): __destroy auto-runs ~T() on each element
*   - List<T*>(): non-owning, you must delete each T* manually
*   - List<T*>::MakeOwning(): owning, auto-deletes each T* on list delete
*
* Thread safety: None. External synchronization required for shared access.
 */

#ifndef CLASSYC_LIST_H
#define CLASSYC_LIST_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ── Runtime dict helpers (resolved at link / JIT time) ────────────────────
 * Declared with the ClassyC `dict` type on purpose.  A value produced by these
 * (`dict d = dict_create_object()`) must keep its dict identity; declaring them
 * `struct DictValue*` instead silently breaks dict_object_set / dict_array_append
 * (the result is treated as a plain pointer rather than a tagged dict, so writes
 * land on the wrong thing and rows come back empty).  This is the single source
 * of truth shared coherently with sqlite.h. */
dict dict_create_array(void);
dict dict_create_object(void);
dict dict_create_null(void);
dict dict_create_bool(int b);
dict dict_create_int64(long n);
dict dict_create_number(double n);
dict dict_create_string(char *s);
int  dict_array_append(dict array_val, dict new_val);
int  dict_object_set(dict obj_val, char *key, dict new_val);
void dict_destroy(dict v);

class List<T> {
    T*  data;
    int length;
    int capacity;
    int _owns_ptrs;  /* ownership flag: 1 = delete pointer elements on dtor */

    /* ═══════════════════════════ Constructors ═══════════════════════════ */

    /* Default: empty list with initial capacity of 4. Non-owning. */
    List() {
        this->length   = 0;
        this->capacity = 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
    }

    /* Pre-sized: empty list with the given initial capacity. Non-owning.
     * Clamps non-positive values to the default capacity of 4. */
    List(int initialCapacity) {
        this->length   = 0;
        this->capacity = initialCapacity > 0 ? initialCapacity : 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
    }

    /* Singleton: list pre-loaded with one element. Non-owning.
     * For T=int, the capacity constructor wins tie-breaking on integer literals. */
    List(T firstItem) {
        this->length   = 0;
        this->capacity = 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
        this->data[0]  = firstItem;
        this->length   = 1;
    }

    /* Array view: copy the elements of a plain C array (or slice). Non-owning.
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
        this->_owns_ptrs = 0;
        for (int i = 0; i < n; i++) {
            this->data[i] = items[i];
            this->length++;
        }
    }



    /* ═══════════════════════════ Destructor ═════════════════════════════════ */

    /* Destroy each live element, then release the backing buffer.
     *
     * __destroy(x) is a compiler intrinsic: for a by-value class element type
     * with a destructor it runs that destructor on x; for scalars, String, and
     * pointer element types it expands to nothing.  This is what makes
     * `delete list` (or a List on a `defer delete`) reclaim its by-value class
     * elements — owned storage dies with the owner.
     *
     * is_pointer<T>() is a compiler intrinsic that returns 1 if T is a pointer
     * type, 0 otherwise. Combined with the _owns_ptrs flag, this enables automatic
     * deletion of owned pointer elements. */
    ~List() {
        for (int i = 0; i < this->length; i++) {
            if (this->_owns_ptrs && is_pointer<T>()) {
                delete this->data[i];  /* delete owned pointer elements */
            } else {
                __destroy(this->data[i]);  /* by-value or non-owned */
            }
        }
        if (this->data) free((void*) this->data);
    }

    /* ═══════════════════════════ Accessors ═════════════════════════════ */

    int Count()    { return this->length; }
    int Capacity() { return this->capacity; }
    int IsEmpty()  { return this->length == 0; }

    /* owns(): mark this list as the owner of its pointer elements.
     * Usage: List<Track*>* lib = new List<Track*>().owns();
     * When the list is deleted, it will also delete each T* element
     * (runs the element's destructor, if any, then frees it).
     * No-op for by-value element lists (List<int>, List<Track>) where
     * __destroy already handles cleanup. Returns this for chaining. */
    List<T>* owns() {
        this->_owns_ptrs = 1;
        return this;
    }

    /* Indexed access. Caller must ensure 0 <= index < Count(). */
    T Get(int index) { return this->data[index]; }

    /* Convenience: first/last element. Undefined behavior on empty list. */
    T First() { return this->data[0]; }
    T Last()  { return this->data[this->length - 1]; }

    /* ═════════════════════════ Capacity management ══════════════════════ */

    void owns(bool owns) { this->_owns_ptrs = owns; }

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

    /* ═════════════════════════ Array conversions ═══════════════════════ */

    /* Bulk-copy the live elements into a fresh heap T[] and return it.
     * Mirrors C#'s `T[] List<T>.ToArray()`: the result is an independent
     * copy (mutating it does not touch the list).  The element count is the
     * list's Count() — a bare T* carries no length of its own, so keep
     * Count() around if you need the bound.  Caller owns the result and must
     * `free()` it.  Empty lists return a 1-slot buffer (never NULL), matching
     * the never-null spirit of Array.Empty<T>(). */
    T* ToArray() {
        int n = this->length > 0 ? this->length : 1;
        T* array = (T*) malloc(sizeof(T) * n);
        if (this->length > 0)
            memcpy((void*) array, (void*) this->data, sizeof(T) * this->length);
        return array;
    }

    /* Bulk-copy the live elements into a caller-provided buffer starting at
     * index 0.  Mirrors C#'s `List<T>.CopyTo(T[] array)`.  The destination
     * must have room for at least Count() elements. */
    void CopyTo(T* destination) {
        if (this->length > 0)
            memcpy((void*) destination, (void*) this->data, sizeof(T) * this->length);
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

	    /* Return new heap list with fn(item) applied to every element.
     * Same-type transform (T -> T), so it chains with Filter:
     *   nums->Filter(p)->Map(f).  Caller must `delete` the result. */
	    List<T>* Map(T(*fn)(T)) {
        List<T>* result = new List<T>(this->length > 0 ? this->length : 1);
        for (int i = 0; i < this->length; i++)
            result->Add(fn(this->data[i]));
        return result;
    }

    /* Convert List<dict> to DICT_ARRAY. Casts the backing store to dict* so the
     * generic body type-checks for every element specialization (the method is
     * only meaningful for List<dict>). */
    dict ToDict() {
        dict arr = dict_create_array();
        dict* d = (dict*)this->data;
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, d[i]);
        }
        return arr;
    }

    /* Convert List<String> to a DICT_ARRAY of DICT_STRING values.
     * Idiomatic JSON serialization for string lists:
     *
     *   List<String>* parts = s.split(",");
     *   dict out = { "items": parts->StringsToJsonArray() };
     *   return resp_ok(out.json);
     *
     * The returned dict is a heap-allocated array owned by whatever dict
     * references it (or freed when that dict is deleted).
     *
     * Reads each String through `*(char**)&elem` rather than `(char*)elem`: the
     * address-of + typed-pointer-deref keeps this generic body type-checking
     * for *every* element specialization (a bare `(char*)elem` cast is rejected
     * for floating-point T like List<double>), while reading the real value
     * when the list actually holds Strings. */
    dict StringsToJsonArray() {
        dict arr = dict_create_array();
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, dict_create_string(*(char**)&this->data[i]));
        }
        return arr;
    }

    /* Convert List<int> to a DICT_ARRAY of DICT_INT64 values. The integer
     * sibling of StringsToJsonArray(); reads via `*(int*)&elem` so the body
     * type-checks for all T and round-trips the value for List<int>. */
    dict IntsToJsonArray() {
        dict arr = dict_create_array();
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, dict_create_int64((long)*(int*)&this->data[i]));
        }
        return arr;
    }

    /* Automagical JSON-array conversion: inspect the element type T at compile
     * time via nameof<T>() and emit the matching DICT value for every element
     * (int/long/short -> int64, double/float -> number, String/char* -> string,
     * dict -> passthrough, anything else -> null).  One call converts a
     * List<int>, List<double>, or List<String> with no per-type helper:
     *
     *   dict out = { "items": scores->ToJsonArray() };
     *
     * Each branch reads the element through a typed pointer (`*(int*)&elem`,
     * `*(double*)&elem`, `*(char**)&elem`): those casts are pointer-to-pointer
     * (always valid for any T), and only the nameof-selected branch runs, so
     * the body both type-checks for every specialization and reads correctly. */
    dict ToJsonArray() {
        dict arr = dict_create_array();
        const char* tn = nameof<T>();
        for (int i = 0; i < this->length; i++) {
            T* p = &this->data[i];
            dict v;
            if (strcmp(tn, "String") == 0 || strcmp(tn, "char") == 0)
                v = dict_create_string(*(char**)p);
            else if (strcmp(tn, "double") == 0)
                v = dict_create_number(*(double*)p);
            else if (strcmp(tn, "float") == 0)
                v = dict_create_number((double)*(float*)p);
            else if (strcmp(tn, "long") == 0)
                v = dict_create_int64(*(long*)p);
            else if (strcmp(tn, "short") == 0)
                v = dict_create_int64((long)*(short*)p);
            else if (strcmp(tn, "int") == 0 || strcmp(tn, "unsigned") == 0
                     || strcmp(tn, "bool") == 0)
                v = dict_create_int64((long)*(int*)p);
            else if (strcmp(tn, "dict") == 0)
                v = *(dict*)p;
            else
                v = dict_create_null();
            dict_array_append(arr, v);
        }
        return arr;
    }

    /* Generic projection to a DICT_ARRAY: apply `fn` to every element and
     * append the resulting DictValue* to the array. This is the
     * Select(x => ...).ToArray() of JSON building — StringsToJsonArray() and
     * IntsToJsonArray() are just the common specializations:
     *
     *   dict asNum(int x) { return dict_create_int64(x); }
     *   dict out = { "items": nums->ToJsonArrayBy(asNum) };
     *
     * `fn(item)` is responsible for producing each element value. */
    dict ToJsonArrayBy(dict(*fn)(T)) {
        dict arr = dict_create_array();
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, fn(this->data[i]));
        }
        return arr;
    }

    /* Build a DICT_OBJECT keyed by `keyFn(item)` with values `valFn(item)`.
     * Mirrors C#'s LINQ `ToDictionary(keySelector, valueSelector)`:
     *
     *   const char* nameOf(Track* t) { return (char*)t->title; }
     *   dict idOf(Track* t) { return dict_create_int64(t->id); }
     *   dict byName = library->ToDictBy(nameOf, idOf);
     *
     * Duplicate keys follow dict_object_set semantics: a later element with
     * the same key overwrites the earlier value (last-one-wins, as in C#'s
     * indexer-based fill).  The returned dict is owned by whatever dict
     * references it. */
    dict ToDictBy(const char*(*keyFn)(T), dict(*valFn)(T)) {
        dict obj = dict_create_object();
        for (int i = 0; i < this->length; i++) {
            dict_object_set(obj, (char*)keyFn(this->data[i]), valFn(this->data[i]));
        }
        return obj;
    }

    /* Build a List<T> from a dict DICT_ARRAY, coercing each element to T.  The
     * automagical reverse of ToJsonArray(): the per-element `(T)array[i]` cast
     * unwraps each element to the right type, so this works for List<int>,
     * List<double>, List<String>, and List<dict> (passthrough) with no per-type
     * helper.  Because it is a cast (not a bare `T v = array[i]` assignment) the
     * generic body also type-checks for class element types: `(T)array[i]`
     * lowers to the dict-bind cast for List<SomeClass>.  Caller owns the result
     * (`defer delete`):
     *
     *   List<String>* tags = List<String>.FromJson(req->body.tags);
     *   List<int>*    xs   = List<int>.FromJson(d.xs);
     *
     * `array` should be a JSON array; a scalar/object dict yields a length of 0
     * (or 1) per the dict length() rules. */
    static List<T>* FromJson(dict array) {
        List<T>* r = new List<T>();
        int n = (int)array.length();
        for (int i = 0; i < n; i++) {
            r->Add((T)array[i]);
        }
        return r;
    }

    /* Serialize the list to a JSON-array String, e.g. "[1,2,3]" or
     * '["a","b"]'.  Builds a transient dict via ToJsonArray(), serializes it,
     * then frees it (the returned String is independent and survives):
     *
     *   String body = scores->ToJson();   // "[10,20,30]"
     *
     * Intended for scalar element lists (int/double/String); for a List<dict>
     * use ToDict() and serialize the owning dict yourself (ToJson() would free
     * the referenced element dicts). */
    String ToJson() {
        dict arr = this->ToJsonArray();
        String j = arr.json;
        dict_destroy(arr);
        return j;
    }


};

#endif /* CLASSYC_LIST_H */
