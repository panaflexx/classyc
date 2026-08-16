/* probe-p0b-arg-leak.cy — isolate WHERE class values leak:
 *  1. prvalue call arg:      take(Box(7))
 *  2. named arg:             Box b; take(b)     (is the param destroyed?)
 *  3. callee-local:          Box created inside fn, destroyed at return?
 *  4. named init from method: Owns c = xs.Get(i)  (RAII on call-returned value)
 *
 * Run: ./bin/classyc -g -I include sketch/probe-p0b-arg-leak.cy -eg
 */
#include <stdio.h>
#include "list.h"

int ctors = 0, dtors = 0;
[[copyable_no_release]] /* counting dtor only */
class Box {
    int id;
    Box(int id) { this.id = id; ctors++; }
    ~Box() { dtors++; }
    int getId() { return id; }
};

void take(Box b) {
    printf("    inside take: id=%d ctors=%d dtors=%d\n", b.getId(), ctors, dtors);
}

void make_local() {
    Box z = Box(99);
    printf("    inside make_local: ctors=%d dtors=%d\n", ctors, dtors);
}

Box make_ret(int id) {
    Box r = Box(id);
    return r;
}

int main() {
    printf("=== probe P0b: isolate the leak ===\n\n");

    printf("-- 1. prvalue arg take(Box(7)) --\n");
    { int c0 = ctors, d0 = dtors;
      take(Box(7));
      printf("  after: +ctors=%d +dtors=%d  %s\n", ctors-c0, dtors-d0,
             (ctors-c0)==(dtors-d0) ? "OK" : "*** LEAK ***"); }

    printf("-- 2. named arg take(b) --\n");
    { int c0 = ctors, d0 = dtors;
      { Box b = Box(8); take(b); }
      printf("  after: +ctors=%d +dtors=%d  %s\n", ctors-c0, dtors-d0,
             (ctors-c0)==(dtors-d0) ? "OK" : "*** LEAK (param slot never destroyed) ***"); }

    printf("-- 3. callee-local Box destroyed at return --\n");
    { int c0 = ctors, d0 = dtors;
      make_local();
      printf("  after: +ctors=%d +dtors=%d  %s\n", ctors-c0, dtors-d0,
             (ctors-c0)==(dtors-d0) ? "OK" : "*** LEAK ***"); }

    printf("-- 4. by-value return: Box r = make_ret(5) --\n");
    { int c0 = ctors, d0 = dtors;
      { Box r = make_ret(5);
        printf("    r.id=%d\n", r.getId()); }
      printf("  after: +ctors=%d +dtors=%d  %s\n", ctors-c0, dtors-d0,
             (ctors-c0)==(dtors-d0) ? "OK" : "*** LEAK ***"); }

    printf("-- 5. named init from method: Box g = xs.Get(0) --\n");
    { int c0 = ctors, d0 = dtors;
      { auto xs = List<Box>();
        xs.Add(Box(42));
        { Box g = xs.Get(0);
          printf("    g.id=%d, inside: +ctors=%d +dtors=%d\n",
                 g.getId(), ctors-c0, dtors-d0); }
        printf("  after g scope: +ctors=%d +dtors=%d\n", ctors-c0, dtors-d0); }
      printf("  after list scope: +ctors=%d +dtors=%d  %s\n", ctors-c0, dtors-d0,
             (ctors-c0)==(dtors-d0) ? "OK" : "*** IMBALANCE ***"); }

    printf("\ntotal ctors=%d dtors=%d %s\n", ctors, dtors,
           ctors==dtors ? "OK" : "*** GLOBAL LEAK ***");
    return ctors - dtors;
}
