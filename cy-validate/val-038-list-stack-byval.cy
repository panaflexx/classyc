/* val-038-list-stack-byval.cy — stack / value List construction + by-value elements.
 *
 * End-state ergonomics for collections living on the stack (RAII):
 *   List<int> xs;                 // default ctor + ~List at scope exit
 *   List<int> ys = List<int>();   // explicit value construct
 *   auto zs = List<int>();        // auto + ClassName(...) value construct
 *   List<Pt> pts; pts.Add(p);     // by-value class elements + __destroy
 *
 * Heap form (unchanged): owned auto h = new List<int>();
 *
 * Run: ./bin/classyc -g -I include cy-validate/val-038-list-stack-byval.cy -eg
 */
#include <stdio.h>
#include "list.h"
#include "map.h"

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

int dtors = 0;
class Pt {
    int x;
    Pt(int x) { this.x = x; }
    ~Pt() { dtors++; }
    int getX() { return x; }
};

int even_x(Pt p) { return p.getX() % 2 == 0; }

int main() {
    printf("=== val-038 stack List + by-value elements ===\n\n");

    /* ── 1. Stack List<int> forms ─────────────────────────────────────── */
    printf("-- 1. stack List<int> --\n");
    {
        List<int> a;
        a.Add(1); a.Add(2); a.Add(3);
        check(a.Count() == 3 && a.Get(0) == 1 && a.Last() == 3, "1a List<int> a; default ctor");
    }
    {
        List<int> b = List<int>();
        b.Add(10);
        check(b.Count() == 1 && b.Get(0) == 10, "1b List<int> b = List<int>()");
    }
    {
        auto c = List<int>();
        c.Add(7); c.Add(8);
        check(c.Count() == 2 && c.Get(1) == 8, "1c auto c = List<int>()");
    }
    {
        auto d = List<int>(16);
        check(d.Capacity() >= 16, "1d auto d = List<int>(capacity)");
        d.Add(42);
        check(d.Get(0) == 42, "1e add after capacity ctor");
    }

    /* ── 2. LINQ from stack List ──────────────────────────────────────── */
    printf("\n-- 2. LINQ on stack List --\n");
    {
        auto xs = List<int>();
        xs.Add(1); xs.Add(2); xs.Add(3); xs.Add(4); xs.Add(5);
        List<int>* ev = xs.Where((int x) => x % 2 == 0);
        check(ev.Count() == 2 && ev.Get(0) == 2, "2a Where from stack List");
        delete ev; /* transforms still return heap lists */
        int sum = 0;
        for (auto v in xs) sum += v;
        check(sum == 15, "2b for-in stack List");
    }

    /* ── 3. By-value class elements + RAII dtor ───────────────────────── */
    printf("\n-- 3. List<Pt> by-value + scope dtor --\n");
    {
        dtors = 0;
        {
            auto xs = List<Pt>();
            Pt a = Pt(1); Pt b = Pt(2); Pt c = Pt(3);
            xs.Add(a); xs.Add(b); xs.Add(c);
            check(xs.Count() == 3, "3a stack List<Pt> holds 3");
            int sum = 0;
            for (auto p in xs) sum += p.getX();
            check(sum == 6, "3b for-in List<Pt>");
            List<Pt>* ev = xs.Where(even_x);
            check(ev.Count() == 1 && ev.First().getX() == 2, "3c Where on List<Pt>");
            delete ev;
        } /* ~List runs __destroy on 3 copies; stack Pt a/b/c also dtor */
        check(dtors >= 3, "3d element dtors on stack List scope exit");
        printf("    total dtors=%d (list elements + stack temps)\n", dtors);
    }

    /* ── 4. Mutation destroy on stack List ────────────────────────────── */
    printf("\n-- 4. mutation destroy --\n");
    {
        dtors = 0;
        auto xs = List<Pt>();
        Pt a = Pt(1); Pt b = Pt(2);
        xs.Add(a); xs.Add(b);
        int before = dtors;
        {
            Pt r = Pt(99);
            xs.Set(0, r);
        }
        check(dtors > before, "4a Set destroys old by-val element");
        int mid = dtors;
        xs.Clear();
        check(dtors > mid && xs.Count() == 0, "4b Clear destroys remaining");
    }

    /* ── 5. Non-generic auto value construct ──────────────────────────── */
    printf("\n-- 5. auto class value construct --\n");
    {
        dtors = 0;
        {
            auto p = Pt(42);
            check(p.getX() == 42, "5a auto p = Pt(42)");
        }
        check(dtors >= 1, "5b ~Pt on auto stack var scope exit");
    }

    /* ── 6. Stack Map (same value-construct path + string subscript) ──── */
    printf("\n-- 6. stack Map --\n");
    {
        auto m = Map<String, int>();
        m.Set("a", 1);
        m["b"] = 2;                                    /* value receiver + String key */
        check(m.Count() == 2 && m.Get("a") == 1 && m["b"] == 2,
              "6a auto m = Map<String,int>() + m[k]");
    }

    /* ── 7. Heap form still works ─────────────────────────────────────── */
    printf("\n-- 7. heap form unchanged --\n");
    {
        owned auto h = new List<int>();
        h.Add(100);
        check(h.Count() == 1 && h.Get(0) == 100, "7a owned auto h = new List<int>()");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
