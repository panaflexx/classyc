/* classy-generics.c — production-ready generic List<T>
 *
 * Full API surface (30 methods across 6 categories):
 *
 *   Constructors  List()  List(int cap)  List(T first)
 *   Accessors     Count  Capacity  IsEmpty  Get  First  Last
 *   Capacity ops  EnsureCapacity  TrimExcess
 *   Mutation      Set  Add  Insert  Pop  RemoveAt  Remove  Clear
 *   Search        IndexOf  LastIndexOf  Contains
 *   Transforms    Reverse  Sort  Concat  Slice  Copy  Equals
 *   Higher-order  ForEach  Filter
 *
 * Compiler features exercised:
 *   · Three overloaded constructors selected by argument-type scoring.
 *   · Self-referential generic parameters: List<T>* in Concat, Slice, Copy,
 *     Equals, Filter — enabled by pre-registering the template before its body
 *     is parsed and resolving the "__generic_List_T" placeholder in specialize_node.
 *   · Typed function-pointer parameters: int(*cmp)(T,T) in Sort,
 *     void(*action)(T) in ForEach, int(*pred)(T) in Filter.
 *   · for-in duck-typing protocol (Count/Get) in Concat.
 *   · Brace-init Add-protocol for literal construction.
 *
 * Usage:  ./bin/classyc examples/classy-generics.c -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Generic dynamic-array class ────────────────────────────────────────── */

class List<T> {
    T*  data;
    int length;
    int capacity;

    /* ═══════════════════════════ Constructors ═══════════════════════════ */

    /* Default: empty list with capacity 4. */
    List() {
        this.length   = 0;
        this.capacity = 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }

    /* Pre-sized: empty list with the given initial capacity.
       Useful when the expected element count is known upfront. */
    List(int initialCapacity) {
        this.length   = 0;
        this.capacity = initialCapacity > 0 ? initialCapacity : 4;
        this.data     = (T*) malloc(sizeof(T) * this.capacity);
    }

    /* Singleton: list pre-loaded with one element.
       For T=int an integer literal ties with List(int cap) on score; the
       capacity ctor (registered first) wins — which is the right default.
       For String/double the T ctor wins unambiguously (score 3 vs 1).
       First-element placement is inlined to avoid a forward-declaration
       dependency on Add. */
    List(T firstItem) {
        this.length      = 0;
        this.capacity    = 4;
        this.data        = (T*) malloc(sizeof(T) * this.capacity);
        this.data[0]     = firstItem;
        this.length      = 1;
    }

    /* ═══════════════════════════ Destructor ════════════════════════════ */

    ~List() {
        if (this.data) free((void*) this.data);
    }

    /* ═══════════════════════════ Accessors ═════════════════════════════ */

    int Count()    { return this.length; }
    int Capacity() { return this.capacity; }
    int IsEmpty()  { return this.length == 0; }

    /* Indexed access — caller must ensure 0 <= index < Count(). */
    T Get(int index) { return this.data[index]; }

    /* Convenience aliases — undefined for empty list. */
    T First() { return this.data[0]; }
    T Last()  { return this.data[this.length - 1]; }

    /* ═════════════════════════ Capacity management ══════════════════════ */

    /* Ensure the backing array can hold at least `min` elements.
       Doubles capacity until the requirement is met; existing data is
       preserved.  No-op if the current capacity already suffices. */
    void EnsureCapacity(int min) {
        if (min <= this.capacity) return;
        int newCap = this.capacity;
        while (newCap < min) newCap = newCap * 2;
        T* newData = (T*) malloc(sizeof(T) * newCap);
        for (int i = 0; i < this.length; i++) newData[i] = this.data[i];
        free((void*) this.data);
        this.data     = newData;
        this.capacity = newCap;
    }

    /* Shrink the backing array to exactly Count() (minimum 1).
       Releases wasted memory after bulk removes or a Clear(). */
    void TrimExcess() {
        int target = this.length > 0 ? this.length : 1;
        if (target == this.capacity) return;
        T* newData = (T*) malloc(sizeof(T) * target);
        for (int i = 0; i < this.length; i++) newData[i] = this.data[i];
        free((void*) this.data);
        this.data     = newData;
        this.capacity = target;
    }

    /* ═════════════════════════════ Search ════════════════════════════ */
    /* Declared before the Remove* mutation methods so they can call IndexOf. */

    /* Return the index of the first element equal to `item` (using ==),
       or -1 if not found.
       For String, == is pointer equality; for int/double it is value equality. */
    int IndexOf(T item) {
        for (int i = 0; i < this.length; i++)
            if (this.data[i] == item) return i;
        return -1;
    }

    /* Return the index of the last element equal to `item`, or -1. */
    int LastIndexOf(T item) {
        int i = this.length - 1;
        while (i >= 0) {
            if (this.data[i] == item) return i;
            i--;
        }
        return -1;
    }

    /* Return 1 if any element equals `item` (using ==), 0 otherwise. */
    int Contains(T item) { return this->IndexOf(item) >= 0; }

    /* ═══════════════════════════ Mutation ══════════════════════════════ */

    /* Replace the element at `index`.  No-op if out of range. */
    void Set(int index, T item) {
        if (index >= 0 && index < this.length) this.data[index] = item;
    }

    /* Append `item` to the end.  Grows capacity as needed. */
    void Add(T item) {
        this->EnsureCapacity(this.length + 1);
        this.data[this.length] = item;
        this.length++;
    }

    /* Insert `item` before `index`.
       index <= 0  → prepend;  index >= Count() → append.
       Later elements are shifted right.  Grows capacity as needed. */
    void Insert(int index, T item) {
        if (index < 0)           index = 0;
        if (index > this.length) index = this.length;
        this->EnsureCapacity(this.length + 1);
        for (int i = this.length; i > index; i--) this.data[i] = this.data[i - 1];
        this.data[index] = item;
        this.length++;
    }

    /* Remove and return the last element.  Undefined for empty list. */
    T Pop() {
        this.length--;
        return this.data[this.length];
    }

    /* Remove the element at `index`, shifting later elements left.
       No-op if `index` is out of range. */
    void RemoveAt(int index) {
        if (index < 0 || index >= this.length) return;
        for (int i = index; i < this.length - 1; i++) this.data[i] = this.data[i + 1];
        this.length--;
    }

    /* Remove the first element equal to `item` (using ==).
       Returns 1 if an element was removed, 0 if not found.
       For String, == is pointer equality; pass the pointer from Get()/Add(). */
    int Remove(T item) {
        int idx = this->IndexOf(item);
        if (idx < 0) return 0;
        this->RemoveAt(idx);
        return 1;
    }

    /* Remove all elements; backing capacity is retained. */
    void Clear() { this.length = 0; }

    /* ══════════════════════════ Transformations ═════════════════════════ */

    /* Reverse all elements in place. */
    void Reverse() {
        int lo = 0, hi = this.length - 1;
        while (lo < hi) {
            T tmp = this.data[lo];
            this.data[lo] = this.data[hi];
            this.data[hi] = tmp;
            lo++;
            hi--;
        }
    }

    /* Sort elements in place.
       Uses Shell sort (O(n log² n) average, in-place, no recursion).
       `cmp(a, b)` must return < 0 if a < b, 0 if a == b, > 0 if a > b. */
    void Sort(int(*cmp)(T, T)) {
        int gap = this.length / 2;
        while (gap > 0) {
            for (int i = gap; i < this.length; i++) {
                T   tmp = this.data[i];
                int j   = i;
                while (j >= gap && cmp(this.data[j - gap], tmp) > 0) {
                    this.data[j] = this.data[j - gap];
                    j = j - gap;
                }
                this.data[j] = tmp;
            }
            gap = gap / 2;
        }
    }

    /* Append every element of `other` to this list; returns `this` for
       fluent chaining.  Uses the Count()/Get(int) protocol on `other`, so
       any class exposing those two methods is a valid argument. */
    List<T>* Concat(List<T>* other) {
        for (auto item in other) this->Add(item);
        return this;
    }

    /* Return a new heap-allocated list containing `count` elements starting
       at `start`.  Clamps to valid range.  Caller is responsible for delete. */
    List<T>* Slice(int start, int count) {
        if (start < 0)                   start = 0;
        if (start >= this.length)        count = 0;
        if (count < 0)                   count = 0;
        if (start + count > this.length) count = this.length - start;
        List<T>* result = new List<T>(count > 0 ? count : 1);
        for (int i = 0; i < count; i++) result->Add(this.data[start + i]);
        return result;
    }

    /* Return a shallow copy of this list.  Caller is responsible for delete. */
    List<T>* Copy() {
        List<T>* c = new List<T>(this.length > 0 ? this.length : 1);
        for (int i = 0; i < this.length; i++) c->Add(this.data[i]);
        return c;
    }

    /* Return 1 if `other` has the same Count() and pairwise-equal elements
       (using ==); 0 otherwise. */
    int Equals(List<T>* other) {
        if (other == NULL || other->Count() != this.length) return 0;
        for (int i = 0; i < this.length; i++)
            if (this.data[i] != other->Get(i)) return 0;
        return 1;
    }

    /* ══════════════════════════ Higher-order ═══════════════════════════ */

    /* Call `action(item)` for every element in order. */
    void ForEach(void(*action)(T)) {
        for (int i = 0; i < this.length; i++) action(this.data[i]);
    }

    /* Return a new heap-allocated list containing only the elements for
       which `pred(item) != 0`.  Caller is responsible for delete. */
    List<T>* Filter(int(*pred)(T)) {
        List<T>* result = new List<T>();
        for (int i = 0; i < this.length; i++)
            if (pred(this.data[i])) result->Add(this.data[i]);
        return result;
    }
};

/* ─── Test harness ────────────────────────────────────────────────────────── */

int passed;
int failed;

void check(int cond, char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else       { printf("  FAIL  %s\n", label); failed++; }
}

/* ForEach callbacks: lambdas cannot capture outer locals, so we use globals. */
int g_sum   = 0;
int g_count = 0;

void int_accum(int x)    { g_sum   = g_sum   + x; }
void int_count_pos(int x){ if (x > 0) g_count = g_count + 1; }

/* ─── main ────────────────────────────────────────────────────────────────── */

int main() {
    printf("=== List<T> production test suite ===\n\n");
    passed = 0; failed = 0;

    /* ── 1. Constructors ────────────────────────────────────────────────── */
    printf("-- 1. Constructors --\n");

    List<int>* def = new List<int>();
    check(def->Count()    == 0, "1a  default: Count==0");
    check(def->Capacity() == 4, "1b  default: Capacity==4");
    check(def->IsEmpty()  == 1, "1c  default: IsEmpty==1");
    delete def;

    List<int>* precap = new List<int>(32);
    check(precap->Capacity() == 32, "1d  capacity ctor: Capacity==32");
    check(precap->Count()    == 0,  "1e  capacity ctor: Count==0");
    delete precap;

    /* brace-init uses the zero-arg ctor then calls Add for each element */
    List<String>* si = new List<String>{ "alpha", "beta", "gamma" };
    check(si->Count() == 3,                 "1f  brace-init: Count==3");
    check(strcmp(si->Get(0), "alpha") == 0, "1g  brace-init: Get(0)");
    check(strcmp(si->Get(2), "gamma") == 0, "1h  brace-init: Get(2)");
    delete si;

    /* singleton String: T ctor wins (int ctor can't accept String) */
    List<String>* ss = new List<String>("seed");
    check(ss->Count() == 1,                  "1i  singleton String: Count==1");
    check(strcmp(ss->Get(0), "seed") == 0,   "1j  singleton String: Get(0)");
    delete ss;

    /* singleton double: double literal scores exact (3) vs List(int) arith (1) */
    List<double>* sd = new List<double>(2.71);
    check(sd->Count()    == 1,                          "1k  singleton double: Count==1");
    check(sd->Get(0) > 2.70 && sd->Get(0) < 2.72,      "1l  singleton double: Get(0)≈2.71");
    delete sd;

    /* int literal → capacity ctor wins tie (first-registered) */
    List<int>* capwins = new List<int>(8);
    check(capwins->Count()    == 0, "1m  int literal → capacity ctor");
    check(capwins->Capacity() == 8, "1n  Capacity==8");
    delete capwins;

    /* named-arg ctor */
    List<String>* named = new List<String>(initialCapacity=64);
    check(named->Capacity() == 64, "1o  named-arg ctor: Capacity==64");
    delete named;

    /* ── 2. Accessors: Get / First / Last / IsEmpty ─────────────────────── */
    printf("\n-- 2. Accessors --\n");

    List<int>* acc = new List<int>{ 10, 20, 30, 40, 50 };
    check(acc->Count()    == 5,  "2a  Count==5");
    check(acc->IsEmpty()  == 0,  "2b  IsEmpty==0");
    check(acc->Get(0)     == 10, "2c  Get(0)==10");
    check(acc->Get(4)     == 50, "2d  Get(4)==50");
    check(acc->First()    == 10, "2e  First()==10");
    check(acc->Last()     == 50, "2f  Last()==50");
    delete acc;

    /* ── 3. EnsureCapacity / TrimExcess ─────────────────────────────────── */
    printf("\n-- 3. Capacity management --\n");

    List<int>* cm = new List<int>();
    cm->Add(1); cm->Add(2);                 /* count=2, cap=4 */
    cm->EnsureCapacity(20);
    check(cm->Capacity() >= 20, "3a  EnsureCapacity(20) grew cap");
    check(cm->Count()    == 2,  "3b  Count preserved after EnsureCapacity");
    check(cm->Get(1)     == 2,  "3c  data preserved after EnsureCapacity");

    cm->TrimExcess();
    check(cm->Capacity() == 2,  "3d  TrimExcess: Capacity==Count");
    check(cm->Get(0)     == 1,  "3e  data preserved after TrimExcess");

    cm->Clear();
    check(cm->Count()   == 0,   "3f  Clear: Count==0");
    check(cm->IsEmpty() == 1,   "3g  Clear: IsEmpty==1");
    check(cm->Capacity() >= 2,  "3h  Clear retains capacity");
    delete cm;

    /* ── 4. Set / Add / Insert ───────────────────────────────────────────── */
    printf("\n-- 4. Set / Add / Insert --\n");

    List<int>* mut = new List<int>{ 1, 3, 5 };

    mut->Set(1, 99);
    check(mut->Get(1) == 99, "4a  Set(1,99)");

    mut->Add(7);
    check(mut->Count() == 4,  "4b  Add: Count==4");
    check(mut->Last()  == 7,  "4c  Add: Last()==7");

    /* Insert in the middle */
    mut->Insert(2, 50);
    check(mut->Count() == 5,   "4d  Insert middle: Count==5");
    check(mut->Get(2)  == 50,  "4e  Insert middle: Get(2)==50 (new)");
    check(mut->Get(3)  == 5,   "4f  Insert middle: Get(3)==5 (shifted)");

    /* Prepend */
    mut->Insert(0, 0);
    check(mut->Count()  == 6,  "4g  Prepend: Count==6");
    check(mut->First()  == 0,  "4h  Prepend: First()==0");

    /* Insert beyond end → append */
    mut->Insert(1000, 999);
    check(mut->Last() == 999,  "4i  Insert beyond end appends");
    delete mut;

    /* ── 5. Pop / RemoveAt / Remove ─────────────────────────────────────── */
    printf("\n-- 5. Pop / RemoveAt / Remove --\n");

    List<int>* rm = new List<int>{ 10, 20, 30, 40, 50 };

    int v = rm->Pop();
    check(v           == 50, "5a  Pop returns 50");
    check(rm->Count() == 4,  "5b  Count==4 after Pop");
    check(rm->Last()  == 40, "5c  Last()==40 after Pop");

    rm->RemoveAt(1);             /* remove 20 */
    check(rm->Count() == 3,  "5d  Count==3 after RemoveAt(1)");
    check(rm->Get(1)  == 30, "5e  Get(1)==30 (shifted after RemoveAt)");

    int r = rm->Remove(10);
    check(r           == 1,  "5f  Remove(10) found → returns 1");
    check(rm->Count() == 2,  "5g  Count==2 after Remove");

    r = rm->Remove(999);
    check(r           == 0,  "5h  Remove(999) not found → returns 0");
    check(rm->Count() == 2,  "5i  Count unchanged on miss");

    rm->RemoveAt(-1);            /* OOB: no-op */
    rm->RemoveAt(100);           /* OOB: no-op */
    check(rm->Count() == 2,  "5j  OOB RemoveAt is a no-op");
    delete rm;

    /* ── 6. IndexOf / LastIndexOf / Contains ────────────────────────────── */
    printf("\n-- 6. IndexOf / LastIndexOf / Contains --\n");

    List<int>* srch = new List<int>{ 5, 3, 7, 3, 9, 3 };
    check(srch->IndexOf(3)     == 1,  "6a  IndexOf(3)==1 (first)");
    check(srch->IndexOf(99)    == -1, "6b  IndexOf(99)==-1 (missing)");
    check(srch->LastIndexOf(3) == 5,  "6c  LastIndexOf(3)==5 (last)");
    check(srch->LastIndexOf(5) == 0,  "6d  LastIndexOf(5)==0 (unique)");
    check(srch->Contains(9)    == 1,  "6e  Contains(9)==1");
    check(srch->Contains(8)    == 0,  "6f  Contains(8)==0");
    delete srch;

    /* ── 7. Reverse ─────────────────────────────────────────────────────── */
    printf("\n-- 7. Reverse --\n");

    List<int>* rv = new List<int>{ 1, 2, 3, 4, 5 };
    rv->Reverse();
    check(rv->Get(0) == 5, "7a  Reverse: Get(0)==5");
    check(rv->Get(2) == 3, "7b  Reverse: Get(2)==3 (middle unchanged)");
    check(rv->Get(4) == 1, "7c  Reverse: Get(4)==1");
    rv->Reverse();                         /* double-reverse = original order */
    check(rv->Get(0) == 1, "7d  double Reverse restores order");
    delete rv;

    /* ── 8. Sort ─────────────────────────────────────────────────────────── */
    printf("\n-- 8. Sort --\n");

    List<int>* srt = new List<int>{ 5, 2, 8, 1, 9, 3, 7, 4, 6 };
    srt->Sort((int a, int b) => a < b ? -1 : a > b ? 1 : 0);
    check(srt->Get(0) == 1, "8a  sort asc: Get(0)==1");
    check(srt->Get(4) == 5, "8b  sort asc: Get(4)==5");
    check(srt->Get(8) == 9, "8c  sort asc: Get(8)==9");

    srt->Sort((int a, int b) => a > b ? -1 : a < b ? 1 : 0);
    check(srt->Get(0) == 9, "8d  sort desc: Get(0)==9");
    check(srt->Get(8) == 1, "8e  sort desc: Get(8)==1");
    delete srt;

    List<String>* ws = new List<String>{ "banana", "apple", "cherry", "apricot" };
    ws->Sort((String a, String b) => strcmp(a, b));
    check(strcmp(ws->Get(0), "apple")   == 0, "8f  string sort: Get(0)=='apple'");
    check(strcmp(ws->Get(1), "apricot") == 0, "8g  string sort: Get(1)=='apricot'");
    check(strcmp(ws->Get(3), "cherry")  == 0, "8h  string sort: Get(3)=='cherry'");
    delete ws;

    /* Sort already-sorted list (best-case for Shell sort) */
    List<int>* pre = new List<int>{ 1, 2, 3, 4, 5 };
    pre->Sort((int a, int b) => a < b ? -1 : a > b ? 1 : 0);
    check(pre->Get(0) == 1 && pre->Get(4) == 5, "8i  sort already-sorted");
    delete pre;

    /* ── 9. Concat ───────────────────────────────────────────────────────── */
    printf("\n-- 9. Concat --\n");

    List<int>* ca = new List<int>{ 1, 2, 3 };
    List<int>* cb = new List<int>{ 4, 5, 6 };
    List<int>* cc = new List<int>{ 7, 8 };
    ca->Concat(cb)->Concat(cc);            /* fluent chaining */
    check(ca->Count() == 8,  "9a  chained Concat: Count==8");
    check(ca->Get(0)  == 1,  "9b  Get(0)==1");
    check(ca->Get(5)  == 6,  "9c  Get(5)==6");
    check(ca->Get(7)  == 8,  "9d  Get(7)==8");
    delete ca; delete cb; delete cc;

    List<String>* sa = new List<String>{ "Hello", "World" };
    List<String>* sb = new List<String>{ "C#", "Programming" };
    sa->Concat(sb);
    check(sa->Count() == 4,                       "9e  String Concat: Count==4");
    check(strcmp(sa->Get(2), "C#")          == 0, "9f  String Concat: Get(2)");
    check(strcmp(sa->Get(3), "Programming") == 0, "9g  String Concat: Get(3)");
    delete sa; delete sb;

    /* ── 10. Slice ───────────────────────────────────────────────────────── */
    printf("\n-- 10. Slice --\n");

    List<int>* src = new List<int>{ 10, 20, 30, 40, 50, 60 };

    List<int>* sl1 = src->Slice(1, 3);     /* [20, 30, 40] */
    check(sl1->Count() == 3,   "10a  Slice(1,3): Count==3");
    check(sl1->Get(0)  == 20,  "10b  Slice: Get(0)==20");
    check(sl1->Get(2)  == 40,  "10c  Slice: Get(2)==40");

    src->Set(1, 99);                       /* mutate source → slice is independent */
    check(sl1->Get(0)  == 20,  "10d  Slice is a copy (independent of source)");

    List<int>* sl2 = src->Slice(0, 1);    /* single element */
    check(sl2->Count() == 1,   "10e  Slice single element");

    List<int>* sl3 = src->Slice(100, 3);  /* OOB start → empty */
    check(sl3->Count() == 0,   "10f  Slice OOB start → empty");

    delete src; delete sl1; delete sl2; delete sl3;

    /* ── 11. Copy ────────────────────────────────────────────────────────── */
    printf("\n-- 11. Copy --\n");

    List<int>* orig = new List<int>{ 7, 8, 9 };
    List<int>* cpy  = orig->Copy();
    check(cpy->Count()   == 3,  "11a  Copy: Count==3");
    check(cpy->Get(0)    == 7,  "11b  Copy: Get(0)==7");
    check(orig->Equals(cpy),    "11c  Copy Equals original");
    orig->Set(0, 100);
    check(cpy->Get(0)    == 7,  "11d  Copy is independent of original");
    check(orig->Equals(cpy) == 0, "11e  After mutating orig, Equals→0");
    delete orig; delete cpy;

    /* ── 12. Equals ──────────────────────────────────────────────────────── */
    printf("\n-- 12. Equals --\n");

    List<int>* e1 = new List<int>{ 1, 2, 3 };
    List<int>* e2 = new List<int>{ 1, 2, 3 };
    List<int>* e3 = new List<int>{ 1, 2, 4 };
    List<int>* e4 = new List<int>{ 1, 2 };
    check(e1->Equals(e2) == 1, "12a  Equals same content → 1");
    check(e1->Equals(e3) == 0, "12b  Equals diff element → 0");
    check(e1->Equals(e4) == 0, "12c  Equals shorter other → 0");
    check(e4->Equals(e1) == 0, "12d  Equals longer other → 0");
    delete e1; delete e2; delete e3; delete e4;

    /* ── 13. ForEach ─────────────────────────────────────────────────────── */
    printf("\n-- 13. ForEach --\n");

    List<int>* fns = new List<int>{ 1, 2, 3, 4, 5 };
    g_sum   = 0;
    g_count = 0;
    fns->ForEach(int_accum);
    check(g_sum == 15,  "13a  ForEach sum==15");

    fns->ForEach(int_count_pos);
    check(g_count == 5, "13b  ForEach counted 5 positive ints");

    List<int>* neg = new List<int>{ -3, -1, 2, 0, -5, 4 };
    g_count = 0;
    neg->ForEach(int_count_pos);
    check(g_count == 2, "13c  ForEach counted 2 positives in mixed list");
    delete fns; delete neg;

    /* ── 14. Filter ──────────────────────────────────────────────────────── */
    printf("\n-- 14. Filter --\n");

    List<int>* nums = new List<int>{ 1, 2, 3, 4, 5, 6, 7, 8 };

    List<int>* evens = nums->Filter((int x) => x % 2 == 0);
    check(evens->Count() == 4,  "14a  Filter evens: Count==4");
    check(evens->Get(0)  == 2,  "14b  Filter evens: Get(0)==2");
    check(evens->Get(3)  == 8,  "14c  Filter evens: Get(3)==8");

    List<int>* big = nums->Filter((int x) => x > 5);
    check(big->Count() == 3,    "14d  Filter >5: Count==3");
    check(big->Get(0)  == 6,    "14e  Filter >5: Get(0)==6");

    List<int>* none = nums->Filter((int x) => x > 100);
    check(none->Count() == 0,   "14f  Filter no-match: Count==0");

    delete nums; delete evens; delete big; delete none;

    /* String Filter: keep words starting with 'a' */
    List<String>* fruit = new List<String>{ "apple", "banana", "avocado", "cherry", "apricot" };
    List<String>* awords = fruit->Filter((String s) => ((char*)s)[0] == 'a');
    check(awords->Count() == 3,                   "14g  String filter 'a*': Count==3");
    check(strcmp(awords->Get(0), "apple")   == 0, "14h  String filter: Get(0)=='apple'");
    check(strcmp(awords->Get(2), "apricot") == 0, "14i  String filter: Get(2)=='apricot'");
    delete fruit; delete awords;

    /* ── 15. Composition ─────────────────────────────────────────────────── */
    printf("\n-- 15. Composition --\n");

    /* Sort → Reverse → Slice */
    List<int>* comp = new List<int>{ 5, 1, 8, 3, 9, 2, 7, 4, 6 };
    comp->Sort((int a, int b) => a < b ? -1 : a > b ? 1 : 0);  /* 1..9 */
    comp->Reverse();                                              /* 9..1 */
    List<int>* top3 = comp->Slice(0, 3);                         /* 9,8,7 */
    check(top3->Count() == 3,  "15a  Sort→Reverse→Slice: Count==3");
    check(top3->Get(0)  == 9,  "15b  top3: Get(0)==9");
    check(top3->Get(2)  == 7,  "15c  top3: Get(2)==7");
    delete comp; delete top3;

    /* Filter → Sort → ForEach */
    List<int>* mixed = new List<int>{ -2, 5, -8, 3, -1, 9, 4 };
    List<int>* pos   = mixed->Filter((int x) => x > 0);  /* 5,3,9,4 */
    pos->Sort((int a, int b) => a < b ? -1 : a > b ? 1 : 0);
    g_sum = 0;
    pos->ForEach(int_accum);
    check(g_sum == 21,        "15d  Filter>0 → Sort → ForEach sum==21");
    check(pos->Get(0) == 3,   "15e  sorted positives: Get(0)==3");
    check(pos->Get(3) == 9,   "15f  sorted positives: Get(3)==9");
    delete mixed; delete pos;

    /* Copy → Concat → TrimExcess → Equals */
    List<int>* base = new List<int>{ 1, 2, 3 };
    List<int>* ext  = new List<int>{ 4, 5, 6 };
    List<int>* full = base->Copy();
    full->Concat(ext);
    full->TrimExcess();
    List<int>* expected = new List<int>{ 1, 2, 3, 4, 5, 6 };
    check(full->Count()         == 6, "15g  Copy→Concat→Trim: Count==6");
    check(full->Capacity()      == 6, "15h  TrimExcess: Capacity==6");
    check(full->Equals(expected),     "15i  result Equals expected");
    delete base; delete ext; delete full; delete expected;

    printf("\n=== results: %d passed, %d failed ===\n", passed, failed);
    return failed;
}
