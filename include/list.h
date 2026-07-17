/* list.h — Generic dynamic-array collection for ClassyC
 *
 * Provides a production-ready List<T> with methods covering:
 *   · Constructors (default, capacity, singleton, array-view)
 *   · Accessors    (Count, Capacity, IsEmpty, Get, GetMut, First, FirstMut, Last)
 *   · Capacity     (EnsureCapacity, TrimExcess)
 *   · Mutation     (Set, Add, Insert, Pop, RemoveAt, Remove, Clear)
 *   · Search       (IndexOf, LastIndexOf, Contains)
 *   · Transform    (Reverse, Sort, Concat, Slice, Copy, Equals)
 *   · Higher-order (ForEach, Filter, Where, CountWhere, …)
 *   · Views        (ListView<T> — non-owning span; View() / CountWhere)
 *
 * Usage:
 *   #include "list.h"
 *
 *   // Stack / value form (preferred locals — ~List runs at scope exit):
 *   auto nums = List<int>();
 *   // or:  List<int> nums;   List<int> nums = List<int>();
 *   nums.Add(42);
 *
 *   // Heap form when you need a pointer / owned binding:
 *   owned auto heap = new List<int>();
 *   heap.Add(42);
 *
 *   // from a C array, explicit array-view constructor:
 *   String arr[3] = {"a", "b", "c"};
 *   List<String>* lst = new List<String>(arr, 3);
 *
 *   // ...or let the compiler supply the count via arr.ToList(), which lowers
 *   // to the same List(T* items, int count) constructor below:
 *   List<String>* lst2 = arr.ToList();
 *
 * Memory: Stack Lists own their buffer; destructor runs at scope exit.
 * Heap Lists: caller owns (owned auto / defer delete / delete).
 * Slice/Copy/Filter/Where/Select/Plus/Take/Skip/… return List<T> by value
 * (RAII shells — no owned/delete needed for local pipelines). Results are
 * always non-owning of T* pointees — they never copy the source .owns() flag.
 *
 * ListView<T> is a non-owning window over a list buffer (or C array):
 *   auto v = fleet.View();                 // borrows fleet.data
 *   int n  = v.CountWhere((Ship s) => s.IsAlive());  // no intermediate List
 *   auto copy = v.ToList();                // or List.FromView(v)
 * The view must not outlive its source.  Prefer CountWhere / Any / All / Find
 * on List or ListView for real-time scans; use Where / ToList when you need a new list.
 *
 * Move-only: bare assign / copy-init of List is an error (buffer alias).
 *   auto b = move a;   or   b = move a;   transfers ownership; source emptied.
 *   auto c = f();  /  return a;   binds or moves a by-value List return.
 * Brace-init supports class ctor expressions:
 *   new List<Pt>{ Pt(1,2), Pt(3,4) };   xs.Add(Pt(5,6));
 *
 * Pointer ownership:
 *   - List of by-value T: __destroy auto-runs element destructor
 *   - List of T-star default: non-owning
 *   - List of T-star with .owns(): owning — delete frees each pointer element
 *   - Pop() transfers the last element out (no list-side destroy/delete)
 *
 * Thread safety: None. External synchronization required for shared access.
 */

#ifndef CLASSYC_LIST_H
#define CLASSYC_LIST_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Content-aware element equality (mirrors Map MAP_EQ):
 *   String  → strcmp (C# string / content equality)
 *   else    → memcmp of the raw T bits (scalars, pointers, by-value types)
 * Using bare `==` on String compares pointers and makes Contains/IndexOf of
 * string *literals* silently fail ("AURORA" vs heap copy of "AURORA"). */
static inline int list_eq_bytes(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n) == 0;
}
static inline int list_eq_str(const void* a, const void* b, size_t n) {
    (void)n;
    const char* x = *(const char* const*)a;
    const char* y = *(const char* const*)b;
    if (x == y)                 return 1;
    if (x == NULL || y == NULL) return 0;
    return strcmp(x, y) == 0;
}
#define LIST_EQ(a, b) \
    (_Generic((a), String: list_eq_str, default: list_eq_bytes)(&(a), &(b), sizeof(a)))

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

/* Forward decl so List.View() can return ListView before the body is defined.
 * Completing definition is after class List (uses List in ToList). */
class ListView<T>;

class List<T> {
    T*  data;
    int length;
    int capacity;
    int _owns_ptrs;  /* ownership flag: 1 = delete pointer elements on dtor */

    /* ═══════════════════════════ Constructors ═══════════════════════════ */

    /* Dense slots use *(data + i), never data[i]: when T is itself a List/Map/Set,
     * T* is a pointer-to-collection and data[i] would lower to Get() sugar.
     * Pointer arithmetic is also SSA-friendly (base + scaled index → one load).
     * Fresh capacity is zeroed so move-assign's ~T on empty LHS is a no-op. */

    /* Default: empty list with initial capacity of 4. Non-owning. */
    List() {
        this->length   = 0;
        this->capacity = 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        if (this->data) memset((void*)this->data, 0, sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
    }

    /* Pre-sized: empty list with the given initial capacity. Non-owning.
     * Clamps non-positive values to the default capacity of 4. */
    List(int initialCapacity) {
        this->length   = 0;
        this->capacity = initialCapacity > 0 ? initialCapacity : 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        if (this->data) memset((void*)this->data, 0, sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
    }

    /* Singleton: list pre-loaded with one element. Non-owning.
     * For T=int, the capacity constructor wins tie-breaking on integer literals. */
    List(T firstItem) {
        this->length   = 0;
        this->capacity = 4;
        this->data     = (T*) malloc(sizeof(T) * this->capacity);
        if (this->data) memset((void*)this->data, 0, sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
        *(this->data + 0) = move firstItem;
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
        if (this->data) memset((void*)this->data, 0, sizeof(T) * this->capacity);
        this->_owns_ptrs = 0;
        for (int i = 0; i < n; i++) {
            *(this->data + i) = move *(items + i);
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
                delete *(this->data + i);
            } else {
                __destroy(*(this->data + i));
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

    /* Get returns T by value.  Move-only T (nested List/Map/Set) is rewritten
     * to deep Copy() on return of storage.  Prefer GetMut to mutate in place. */
    T Get(int index) {
        if (index < 0 || index >= this->length)
            throw(OutOfBoundsException, "List.Get oob");
        return *(this->data + index);
    }

    /* Pointer into the backing store — advanced escape for mutation without
     * re-Set.  Invalidated by reallocation (Add/EnsureCapacity that grows).
     * Prefer the [] sugar:  list[i].field = …  /  list[i].Method()  already
     * lower to GetMut and yield a true element lvalue (not a Get() copy). */
    T* GetMut(int index) __attribute__((da_ignore)) {
        if (index < 0 || index >= this->length)
            throw(OutOfBoundsException, "List.GetMut oob");
        return this->data + index;
    }

    T First() {
        if (this->length == 0) throw(OutOfBoundsException, "First empty");
        return *(this->data + 0);
    }
    T* FirstMut() __attribute__((da_ignore)) {
        if (this->length == 0) throw(OutOfBoundsException, "FirstMut empty");
        return this->data + 0;
    }
    T Last() {
        if (this->length == 0) throw(OutOfBoundsException, "Last empty");
        return *(this->data + (this->length - 1));
    }
    T* LastMut() __attribute__((da_ignore)) {
        if (this->length == 0) throw(OutOfBoundsException, "LastMut empty");
        return this->data + (this->length - 1);
    }
    T GetOr(int index, T fb) {
        if (index < 0 || index >= this->length) return fb;
        return *(this->data + index);
    }
    bool TryGet(int index, T* out) {
        if (!out || index < 0 || index >= this->length) return false;
        /* Storage RHS rewrites to .Copy() for move-only T (no steal). */
        *out = *(this->data + index);
        return true;
    }
    T FirstOr(T fb) {
        if (this->length == 0) return fb;
        return *(this->data + 0);
    }
    T LastOr(T fb) {
        if (this->length == 0) return fb;
        return *(this->data + (this->length - 1));
    }

    /* ═════════════════════════ Capacity management ══════════════════════ */

    List<T>* owns(int v) { this->_owns_ptrs = v ? 1 : 0; return this; }

    /* Ensure capacity >= min. Doubles until satisfied.
     * Bulk memcpy relocates live elements (one MIR block move per growth) —
     * better for SSA than per-element move-assign loops on nested List T. */
    void EnsureCapacity(int min) __attribute__((da_ignore)) {
        if (min <= this->capacity) return;
        int newCap = this->capacity > 0 ? this->capacity : 1;
        while (newCap < min) newCap = newCap * 2;
        T* newData = (T*) malloc(sizeof(T) * newCap);
        if (!newData) return;
        memset((void*)newData, 0, sizeof(T) * newCap);
        if (this->length > 0 && this->data)
            memcpy((void*)newData, (void*)this->data, sizeof(T) * this->length);
        /* Ownership of nested buffers moved with the bytes; do not ~T old slots. */
        free((void*) this->data);
        this->data     = newData;
        this->capacity = newCap;
    }

    /* Shrink backing array to Count() (minimum 1). Releases wasted memory. */
    void TrimExcess() __attribute__((da_ignore)) {
        int target = this->length > 0 ? this->length : 1;
        if (target == this->capacity) return;
        T* newData = (T*) malloc(sizeof(T) * target);
        if (!newData) return;
        memset((void*)newData, 0, sizeof(T) * target);
        if (this->length > 0 && this->data)
            memcpy((void*)newData, (void*)this->data, sizeof(T) * this->length);
        free((void*) this->data);
        this->data     = newData;
        this->capacity = target;
    }

    /* ═════════════════════════ Search ══════════════════════════════════ */

    /* Index of first element equal to item (String = content, else bytes), or -1. */
    int IndexOf(T item) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++)
            if (LIST_EQ(*(this->data + i), item)) return i;
        return -1;
    }

    /* Index of last element equal to item (String = content, else bytes), or -1. */
    int LastIndexOf(T item) __attribute__((da_ignore)) {
        int i = this->length - 1;
        while (i >= 0) {
            if (LIST_EQ(*(this->data + i), item)) return i;
            i--;
        }
        return -1;
    }

    /* True if item is present.  String compares by content (C# string.Contains-style). */
    int Contains(T item) { return this->IndexOf(item) >= 0; }
    int FindIndex(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++)
            if (pred(*(this->data + i))) return i;
        return -1;
    }

    /* ═══════════════════════════ Mutation ═════════════════════════====== */

    void Set(int index, T item) {
        if (index < 0 || index >= this->length)
            throw(OutOfBoundsException, "Set oob");
        if (this->_owns_ptrs && is_pointer<T>()) delete *(this->data + index);
        else __destroy(*(this->data + index));
        *(this->data + index) = move item;
    }

    /* Append to end. Grows capacity as needed. */
    void Add(T item) {
        this->EnsureCapacity(this->length + 1);
        *(this->data + this->length) = move item;
        this->length++;
    }

    /* Insert before index (clamped). Later elements shift right via memmove. */
    void Insert(int index, T item) {
        if (index < 0)            index = 0;
        if (index > this->length) index = this->length;
        this->EnsureCapacity(this->length + 1);
        if (index < this->length) {
            memmove((void*)(this->data + index + 1), (void*)(this->data + index),
                    sizeof(T) * (this->length - index));
            memset((void*)(this->data + index), 0, sizeof(T));
        }
        *(this->data + index) = move item;
        this->length++;
    }

    /* Remove and return the last element. Ownership transfers to the caller. */
    T Pop() {
        if (this->length == 0) throw(OutOfBoundsException, "Pop empty");
        this->length--;
        T item = move *(this->data + this->length);
        memset((void*)(this->data + this->length), 0, sizeof(T));
        return move item;
    }

    void RemoveAt(int index) {
        if (index < 0 || index >= this->length)
            throw(OutOfBoundsException, "RemoveAt oob");
        if (this->_owns_ptrs && is_pointer<T>()) delete *(this->data + index);
        else __destroy(*(this->data + index));
        if (index < this->length - 1) {
            memmove((void*)(this->data + index), (void*)(this->data + index + 1),
                    sizeof(T) * (this->length - index - 1));
        }
        this->length--;
        memset((void*)(this->data + this->length), 0, sizeof(T));
    }

    /* Remove first occurrence of item via ==. Returns 1 on success, 0 if absent. */
    int Remove(T item) {
        int idx = this->IndexOf(item);
        if (idx < 0) return 0;
        this->RemoveAt(idx);
        return 1;
    }

    void Clear() {
        for (int i = 0; i < this->length; i++) {
            if (this->_owns_ptrs && is_pointer<T>()) delete *(this->data + i);
            else __destroy(*(this->data + i));
        }
        if (this->data && this->length > 0)
            memset((void*)this->data, 0, sizeof(T) * this->length);
        this->length = 0;
    }

    /* ═════════════════════════ Transformations ═════════════════════════ */

    /* Reverse elements in place. O(n). Move-swap is correct for nested List T. */
    void Reverse() __attribute__((da_ignore)) {
        int lo = 0, hi = this->length - 1;
        while (lo < hi) {
            T tmp = move *(this->data + lo);
            *(this->data + lo) = move *(this->data + hi);
            *(this->data + hi) = move tmp;
            lo++;
            hi--;
        }
    }

    /* Sort in place using Shell sort (O(n log² n) average).
     * cmp(a, b) returns <0 if a<b, 0 if equal, >0 if a>b.
     * Uses move for nested collection T; simple index math for SSA. */
    void Sort(int(*cmp)(T, T)) __attribute__((da_ignore)) {
        int gap = this->length / 2;
        while (gap > 0) {
            for (int i = gap; i < this->length; i++) {
                T tmp = move *(this->data + i);
                int j = i;
                while (j >= gap && cmp(*(this->data + (j - gap)), tmp) > 0) {
                    *(this->data + j) = move *(this->data + (j - gap));
                    j = j - gap;
                }
                *(this->data + j) = move tmp;
            }
            gap = gap / 2;
        }
    }

    /* Append other onto this list; returns this for chaining (mutating).
     * Prefer Plus(other) when you need a new list (Python-style + semantics). */
    List<T>* Concat(List<T>* other) {
        if (other) {
            int oc = other->Count();
            this->EnsureCapacity(this->length + oc);
            for (int i = 0; i < oc; i++) this->Add(other->Get(i));
        }
        return this;
    }

    void AddRange(List<T>* other) {
        if (!other) return;
        int oc = other->Count();
        this->EnsureCapacity(this->length + oc);
        for (int i = 0; i < oc; i++) this->Add(other->Get(i));
    }

    void InsertRange(int index, List<T>* other) {
        if (!other || other->Count() == 0) return;
        if (index < 0) index = 0;
        if (index > this->length) index = this->length;
        int oc = other->Count();
        this->EnsureCapacity(this->length + oc);
        if (index < this->length) {
            memmove((void*)(this->data + index + oc), (void*)(this->data + index),
                    sizeof(T) * (this->length - index));
            memset((void*)(this->data + index), 0, sizeof(T) * oc);
        }
        for (int i = 0; i < oc; i++)
            *(this->data + index + i) = other->Get(i); /* prvalue bind / Copy */
        this->length += oc;
    }

    /* Return a by-value list with [start, start+count). Clamps to valid range.
     * Always non-owning of pointees (does not copy .owns()). RAII shell. */
    List<T> Slice(int start, int count) __attribute__((da_ignore)) {
        if (start < 0)                    start = 0;
        if (start >= this->length)        count = 0;
        if (count < 0)                    count = 0;
        if (start + count > this->length) count = this->length - start;
        auto result = List<T>(count > 0 ? count : 1);
        for (int i = 0; i < count; i++)
            result.Add(this->Get(start + i));
        return move result;
    }

    /* Deep-enough copy into a by-value list (Get copies move-only T). */
    List<T> Copy() __attribute__((da_ignore)) {
        auto c = List<T>(this->length > 0 ? this->length : 1);
        for (int i = 0; i < this->length; i++)
            c.Add(this->Get(i));
        return move c;
    }

    /* By-value list = this ++ other (non-mutating).
     * Stand-in for operator+ until the language gets overloaded +.
     * Declared after Copy/AddRange (method order matters). */
    List<T> Plus(List<T>* other) __attribute__((da_ignore)) {
        auto r = this->Copy();
        if (other) r.AddRange(other);
        return move r;
    }

    /* ═════════════════════════ Array conversions ═══════════════════════ */

    T* ToArray() {
        int n = this->length > 0 ? this->length : 1;
        T* array = (T*) malloc(sizeof(T) * n);
        if (array) {
            memset((void*)array, 0, sizeof(T) * n);
            for (int i = 0; i < this->length; i++)
                *(array + i) = this->Get(i);
        }
        return array;
    }
    void CopyTo(T* destination) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++)
            *(destination + i) = this->Get(i);
    }
    /* Element-wise equality; String elements use content compare via LIST_EQ. */
    int Equals(List<T>* other) __attribute__((da_ignore)) {
        if (!other || other->Count() != this->length) return 0;
        for (int i = 0; i < this->length; i++) {
            T b = other->Get(i);
            if (!LIST_EQ(*(this->data + i), b)) return 0;
        }
        return 1;
    }
    /* Unique elements; String uniqueness is by content (via IndexOf/LIST_EQ). */
    List<T> Distinct() __attribute__((da_ignore)) {
        auto r = List<T>(this->length > 0 ? this->length : 4);
        for (int i = 0; i < this->length; i++) {
            if (r.IndexOf(this->Get(i)) < 0)
                r.Add(this->Get(i));
        }
        return move r;
    }

    /* ═════════════════════════ Higher-order ═══════════════════════════ */

    /* Call action(item) for each element in order. */
    void ForEach(void(*action)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            action(item);
        }
    }

    /* By-value list of elements where pred(item) != 0. Always non-owning of T*. */
    List<T> Filter(int(*pred)(T)) __attribute__((da_ignore)) {
        auto result = List<T>();
        for (int i = 0; i < this->length; i++) {
            /* Call Get twice rather than `T item = Get(i)`: a named by-value
               class local with a user dtor is RAII-registered and, with current
               aggregate call/return codegen, can corrupt monomorphized filter
               results.  pred/Add take value params that copy from Get. */
            if (pred(this->Get(i))) result.Add(this->Get(i));
        }
        return move result;
    }

    /* By-value list with fn(item) applied to every element (T → T).
     * Chains with Filter:  auto r = nums.Filter(p).Map(f); */
    List<T> Map(T(*fn)(T)) __attribute__((da_ignore)) {
        auto result = List<T>(this->length > 0 ? this->length : 1);
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            result.Add(fn(item));
        }
        return move result;
    }

    /* Where == Filter. Non-owning view for T*; by-value T is copied.
     * Allocates a new List — for counts use CountWhere / View().CountWhere. */
    List<T> Where(int(*pred)(T)) __attribute__((da_ignore)) {
        auto result = List<T>();
        for (int i = 0; i < this->length; i++) {
            if (pred(this->Get(i))) result.Add(this->Get(i));
        }
        return move result;
    }

    /* Non-allocating: how many elements satisfy pred.  Capturing lambdas
     * open-code (Strategy A), same as Where/Any. */
    int CountWhere(int(*pred)(T)) __attribute__((da_ignore)) {
        int n = 0;
        for (int i = 0; i < this->length; i++) {
            if (pred(this->Get(i))) n++;
        }
        return n;
    }

    /* Non-owning span over this list's buffer.  Must not outlive the List
     * (or survive Add that reallocates).  Cheap to copy. */
    ListView<T> View() __attribute__((da_ignore)) {
        return ListView<T>(this->data, this->length);
    }

    /* Materialize a ListView into a fresh List (also ListView.ToList()). */
    static List<T> FromView(ListView<T> v) __attribute__((da_ignore)) {
        auto result = List<T>();
        int n = v.Count();
        int i;
        for (i = 0; i < n; i++)
            result.Add(v.Get(i));
        return move result;
    }

    /* LINQ-style projection: T → U.  U is a method type parameter (specialized
     * at the call site): xs.Select<String>(toName) or xs.Select(fn) with U
     * inferred from fn's return type.  Returns List<U> by value (RAII). */
    List<U> Select<U>(U(*fn)(T)) __attribute__((da_ignore)) {
        auto result = List<U>(this->length > 0 ? this->length : 1);
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            result.Add(fn(item));
        }
        return move result;
    }

    /* Same-type Select / Map (T → T) — kept as Map() for chains that stay on T. */

    /* Compat: T → String projection. Prefer Select<String>(fn).
     * Nested concrete List<String> by-value shells inside a List<T> method body
     * still hit a monomorphization edge; keep the heap-pointer return here.
     * Call sites use `owned auto` or `delete`, or open-code Select. */
    List<String>* SelectString(String(*fn)(T)) __attribute__((da_ignore)) {
        List<String>* result = new List<String>(this->length > 0 ? this->length : 4);
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            result->Add(fn(item));
        }
        return result;
    }

    /* NOTE: GroupBy for List lives as free GroupBy<T,G> / ListGroupBy in map.h
     * (list.h cannot #include map.h — map includes list).  With UFCS:
     *   nums->GroupBy(keyFn)   // method-style
     *   GroupBy(nums, keyFn)   // free form
     * Prefer Map.GroupBy when the source is already a map. */

    int Any(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            if (pred(item)) return 1;
        }
        return 0;
    }

    int All(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            if (!pred(item)) return 0;
        }
        return 1;
    }

    T Find(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            if (pred(item)) return item;
        }
        T z;
        memset((void*)&z, 0, sizeof(T));
        return z;
    }

    T FindOr(T fb, int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            if (pred(item)) return item;
        }
        return fb;
    }

    static List<T> Repeat(T item, int count) __attribute__((da_ignore)) {
        if (count < 0) count = 0;
        auto r = List<T>(count > 0 ? count : 4);
        for (int i = 0; i < count; i++) r.Add(item);
        return move r;
    }
    static List<T> Range(int start, int count) __attribute__((da_ignore)) {
        if (count < 0) throw(OutOfBoundsException, "List.Range count < 0");
        const char* tn = nameof<T>();
        if (strcmp(tn, "int") != 0 && strcmp(tn, "short") != 0 && strcmp(tn, "long") != 0
                && strcmp(tn, "unsigned") != 0 && strcmp(tn, "bool") != 0) {
            throw(RuntimeException, "List.Range requires integral T");
        }
        auto r = List<T>(count > 0 ? count : 4);
        for (int i = 0; i < count; i++) {
            int v = start + i;
            r.Add(*(T*)&v);
        }
        return move r;
    }

    List<T> Take(int count) __attribute__((da_ignore)) {
        if (count < 0) count = 0;
        if (count > this->length) count = this->length;
        auto result = List<T>(count > 0 ? count : 4);
        for (int i = 0; i < count; i++) result.Add(Get(i));
        return move result;
    }

    List<T> Skip(int count) __attribute__((da_ignore)) {
        if (count < 0) count = 0;
        if (count >= this->length) {
            auto empty = List<T>(4);
            return move empty;
        }
        int remaining = this->length - count;
        auto result = List<T>(remaining);
        for (int i = count; i < this->length; i++) result.Add(Get(i));
        return move result;
    }

    /* Alias of ToJsonArray() — automagic per-element typeof conversion.
     * Body open-coded (same-class call gen can fail on method FIELD def_node). */
    dict ToArrayDict() __attribute__((da_ignore)) {
        dict arr = dict_create_array();
        const char* tn = nameof<T>();
        for (int i = 0; i < this->length; i++) {
            T* p = this->data + i;
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

    /* Convert List<dict> to DICT_ARRAY. Casts the backing store to dict* so the
     * generic body type-checks for every element specialization (the method is
     * only meaningful for List<dict>). */
    dict ToDict() __attribute__((da_ignore)) {
        dict arr = dict_create_array();
        dict* d = (dict*)this->data;
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, *(d + i));
        }
        return arr;
    }

    /* Convert List<String> to a DICT_ARRAY of DICT_STRING values.
     * Idiomatic JSON serialization for string lists:
     *
     *   List<String>* parts = s.split(",");
     *   dict out = { "items": parts->StringsToJsonArray() };
     *   return resp_ok(out.json());
     *
     * The returned dict is a heap-allocated array owned by whatever dict
     * references it (or freed when that dict is deleted).
     *
     * Reads each String through `*(char**)&elem` rather than `(char*)elem`: the
     * address-of + typed-pointer-deref keeps this generic body type-checking
     * for *every* element specialization (a bare `(char*)elem` cast is rejected
     * for floating-point T like List<double>), while reading the real value
     * when the list actually holds Strings. */
    dict StringsToJsonArray() __attribute__((da_ignore)) {
        dict arr = dict_create_array();
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, dict_create_string(*(char**)(this->data + i)));
        }
        return arr;
    }

    /* Convert List<int> to a DICT_ARRAY of DICT_INT64 values. The integer
     * sibling of StringsToJsonArray(); reads via `*(int*)&elem` so the body
     * type-checks for all T and round-trips the value for List<int>. */
    dict IntsToJsonArray() __attribute__((da_ignore)) {
        dict arr = dict_create_array();
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, dict_create_int64((long)*(int*)(this->data + i)));
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
    dict ToJsonArray() __attribute__((da_ignore)) {
        dict arr = dict_create_array();
        const char* tn = nameof<T>();
        for (int i = 0; i < this->length; i++) {
            T* p = this->data + i;
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
    dict ToJsonArrayBy(dict(*fn)(T)) __attribute__((da_ignore)) {
        dict arr = dict_create_array();
        for (int i = 0; i < this->length; i++) {
            dict_array_append(arr, fn(*(this->data + i)));
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
    dict ToDictBy(const char*(*keyFn)(T), dict(*valFn)(T)) __attribute__((da_ignore)) {
        dict obj = dict_create_object();
        for (int i = 0; i < this->length; i++) {
            dict_object_set(obj, (char*)keyFn(*(this->data + i)),
                            valFn(*(this->data + i)));
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
    /* Scalar/String/dict elements only.  Avoids `(T)array[i]` so monomorphs of
     * List_List_Ship (and other class/nested T) do not enter dict-class bind
     * during gen.  Class-field JSON binding still uses (List*)d Add protocol. */
    static List<T>* FromJson(dict array) __attribute__((da_ignore)) {
        List<T>* r = new List<T>();
        const char* tn = nameof<T>();
        int n = (int)array.length();
        for (int i = 0; i < n; i++) {
            T slot;
            memset((void*)&slot, 0, sizeof(T));
            if (strcmp(tn, "int") == 0 || strcmp(tn, "unsigned") == 0
                || strcmp(tn, "bool") == 0)
                *(int*)&slot = (int)array[i];
            else if (strcmp(tn, "short") == 0)
                *(short*)&slot = (short)(int)array[i];
            else if (strcmp(tn, "long") == 0)
                *(long*)&slot = (long)array[i];
            else if (strcmp(tn, "double") == 0)
                *(double*)&slot = (double)array[i];
            else if (strcmp(tn, "float") == 0)
                *(float*)&slot = (float)(double)array[i];
            else if (strcmp(tn, "String") == 0 || strcmp(tn, "char") == 0)
                *(char**)&slot = (char*)array[i];
            else if (strcmp(tn, "dict") == 0)
                *(dict*)&slot = array[i];
            else
                continue; /* nested/class T: skip element */
            r->Add(slot); /* scalar/String copy; no move (ownership pass) */
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
    String ToJson() __attribute__((da_ignore)) {
        dict arr = this->ToJsonArray();
        String j = arr.json();
        dict_destroy(arr);
        return j;
    }

    String ToString() __attribute__((da_ignore)) {
        return this->ToJson();
    }

    String to_string() __attribute__((da_ignore)) {
        return this->ToJson();
    }

};

/* ── ListView<T> — non-owning span over T[length] ──────────────────────────
 * Cheap to copy (pointer + length).  Does not free data.  Lifetime ≤ source.
 * Implements Count/Get so for-in and HOF open-code work like List.
 * Defined after List so ToList() can return List<T> by value. */
class ListView<T> {
    T*  data;
    int length;

    ListView() {
        this->data = NULL;
        this->length = 0;
    }
    /* Borrow `n` elements starting at `items`.  Non-owning. */
    ListView(T* items, int n) {
        this->data = items;
        this->length = n > 0 ? n : 0;
    }
    ~ListView() { /* non-owning — never free data */ }

    int Count()   { return this->length; }
    int IsEmpty() { return this->length == 0; }

    T Get(int index) {
        if (index < 0 || index >= this->length)
            throw(OutOfBoundsException, "ListView.Get oob");
        return *(this->data + index);
    }
    T* GetMut(int index) __attribute__((da_ignore)) {
        if (index < 0 || index >= this->length)
            throw(OutOfBoundsException, "ListView.GetMut oob");
        return this->data + index;
    }
    T First() {
        if (this->length == 0) throw(OutOfBoundsException, "ListView.First empty");
        return *(this->data + 0);
    }
    T Last() {
        if (this->length == 0) throw(OutOfBoundsException, "ListView.Last empty");
        return *(this->data + (this->length - 1));
    }

    /* Sub-window — still non-owning. */
    ListView<T> Slice(int start, int count) __attribute__((da_ignore)) {
        if (start < 0) start = 0;
        if (start > this->length) start = this->length;
        if (count < 0) count = 0;
        if (start + count > this->length) count = this->length - start;
        return ListView<T>(this->data + start, count);
    }

    /* ── Non-allocating scans (prefer over Where for counts / existence) ── */

    int CountWhere(int(*pred)(T)) __attribute__((da_ignore)) {
        int n = 0;
        for (int i = 0; i < this->length; i++) {
            if (pred(this->Get(i))) n++;
        }
        return n;
    }
    int Any(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            if (pred(this->Get(i))) return 1;
        }
        return 0;
    }
    int All(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            if (!pred(this->Get(i))) return 0;
        }
        return 1;
    }
    T Find(int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            if (pred(item)) return item;
        }
        T z;
        memset((void*)&z, 0, sizeof(T));
        return z;
    }
    T FindOr(T fb, int(*pred)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            if (pred(item)) return item;
        }
        return fb;
    }
    void ForEach(void(*action)(T)) __attribute__((da_ignore)) {
        for (int i = 0; i < this->length; i++) {
            T item = this->Get(i);
            action(item);
        }
    }



    /* Materialize into an owning List shell (element copies).  Move-return
     * of List from ListView relies on ensure_class_type_layout so RBLK size
     * is 24 (not 0) when the method was typed while List was still incomplete. */
    List<T> ToList() __attribute__((da_ignore)) {
        auto r = List<T>();
        int i;
        for (i = 0; i < this->length; i++)
            r.Add(this->Get(i));
        return move r;
    }
};


#endif /* CLASSYC_LIST_H */
