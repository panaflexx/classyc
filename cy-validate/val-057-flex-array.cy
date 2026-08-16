/* val-057-flex-array.cy — trailing T a[1] / a[0] / a[] flexible arrays.
 *
 * The C89 struct hack (`T a[1]` as last member) is a live-sized tail, not a
 * one-element array.  With default-on exception guards, `p->a[i]` for i >= 1
 * used to trap as OOB against the declared length.  This file checks:
 *
 *   - heap `T a[1]` (SQLite ExprList shape: nExpr + nAlloc + a[1]) works
 *   - C99 `T a[]` and GNU `T a[0]` still work
 *   - a real local `T loc[1]` is still bounds-checked
 *   - a stack struct's `a[1]` is still size 1 (no extra allocation)
 *   - a sibling capacity (nAlloc) is used as the live bound when present
 *
 * Run:  ./bin/classyc -g -I include cy-validate/val-057-flex-array.cy -eg
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int passed = 0, failed = 0;
void check(int cond, const char *label) {
    if (cond) { printf("  PASS  %s\n", label); passed++; }
    else      { printf("  FAIL  %s\n", label); failed++; }
}

/* SQLite-style: used count + capacity + C89 FAM. */
struct Items {
    int nExpr;
    int nAlloc;
    int a[1];
};

/* Unique sibling `n` — medium-confidence name, only integer field. */
struct Vec {
    int n;
    int a[1];
};

/* Ambiguous integers: usage `i < p->n` should name the bound. */
struct Flagged {
    int flags;
    int n;
    int a[1];
};

struct Items99 {
    int n;
    int a[];
};

struct Items0 {
    int n;
    int a[0];
};

int main() {
    printf("=== val-057 trailing flexible arrays ===\n\n");

    /* Heap C89 FAM: sizeof includes one element; +2 more → 3 slots. */
    {
        struct Items *p = (struct Items *) malloc(sizeof(struct Items) + 2 * sizeof(int));
        p->nAlloc = 3;
        p->nExpr = 3;
        p->a[0] = 10;
        p->a[1] = 20;
        p->a[2] = 30;
        check(p->a[0] == 10 && p->a[1] == 20 && p->a[2] == 30,
              "heap T a[1] FAM: p->a[0..2] (SQLite idiom)");
        {
            int sum = 0, i;
            for (i = 0; i < p->nExpr; i++) sum += p->a[i];
            check(sum == 60, "loop p->a[i] guarded by nExpr");
        }
        {
            int caught = 0;
            try { p->a[p->nAlloc] = 1; }
            catch (OutOfBoundsException e) { caught = 1; }
            check(caught, "heap FAM OOB vs nAlloc");
        }
        {
            /* C one-past-end: `&a[nAlloc]` is a valid pointer, not a load. */
            int *end = &p->a[p->nAlloc];
            check(end == &p->a[0] + p->nAlloc, "FAM &a[nAlloc] is one-past-end, not OOB");
        }
        free(p);
    }

    /* Unique `n` sibling. */
    {
        struct Vec *v = (struct Vec *) malloc(sizeof(struct Vec) + 2 * sizeof(int));
        v->n = 3;
        v->a[0] = 1;
        v->a[1] = 2;
        v->a[2] = 3;
        check(v->a[0] + v->a[1] + v->a[2] == 6, "heap T a[1] with sibling n");
        {
            int caught = 0;
            try { v->a[v->n] = 9; }
            catch (OutOfBoundsException e) { caught = 1; }
            check(caught, "FAM OOB vs unique sibling n");
        }
        free(v);
    }

    /* Usage-based bound: flags + n, loop uses n. */
    {
        struct Flagged *f = (struct Flagged *) malloc(sizeof(struct Flagged) + sizeof(int));
        int i, sum = 0;
        f->flags = 0x1;
        f->n = 2;
        f->a[0] = 4;
        f->a[1] = 5;
        for (i = 0; i < f->n; i++) sum += f->a[i];
        check(sum == 9, "usage i < p->n names the FAM bound");
        {
            int caught = 0;
            try { f->a[f->n] = 0; }
            catch (OutOfBoundsException e) { caught = 1; }
            check(caught, "FAM OOB vs usage-named n");
        }
        free(f);
    }

    /* C99 incomplete tail. */
    {
        struct Items99 *q = (struct Items99 *) malloc(sizeof(struct Items99) + 2 * sizeof(int));
        q->n = 2;
        q->a[0] = 1;
        q->a[1] = 2;
        check(q->a[0] + q->a[1] == 3, "C99 T a[] FAM");
        free(q);
    }

    /* GNU zero-length tail. */
    {
        struct Items0 *r = (struct Items0 *) malloc(sizeof(struct Items0) + 2 * sizeof(int));
        r->n = 2;
        r->a[0] = 7;
        r->a[1] = 8;
        check(r->a[0] + r->a[1] == 15, "GNU T a[0] FAM");
        free(r);
    }

    /* A real one-element local array is still bounds-checked. */
    {
        int loc[1];
        int idx = 1, caught = 0;
        loc[0] = 1;
        try { loc[idx] = 2; }
        catch (OutOfBoundsException e) { caught = 1; }
        check(caught, "local T loc[1] still OOB at [1]");
    }

    /* Stack struct: no extra allocation, a[1] is really one element. */
    {
        struct Items s;
        int idx = 1, caught = 0;
        s.nAlloc = 1;
        s.a[0] = 5;
        try { s.a[idx] = 6; }
        catch (OutOfBoundsException e) { caught = 1; }
        check(caught, "stack struct T a[1] still OOB at [1]");
        check(s.a[0] == 5, "stack struct a[0] is live");
    }

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
