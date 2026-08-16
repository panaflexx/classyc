/* Repro: AOT PIC addrpool must keep &static_fptr_table distinct from
   switch-table slots.  Before the fix, sqlite3DefaultMutex's table
   loaded as strftimeFunc+addend and crashed in sqlite3_initialize. */
#include <stdio.h>

static int add1 (int x) { return x + 1; }
static int add2 (int x) { return x + 2; }

typedef int (*fn_t) (int);

static const struct {
  fn_t a;
  fn_t b;
} tab = { add1, add2 };

static int pick (int n) {
  switch (n) {
  case 0: return 10;
  case 1: return 11;
  case 2: return 12;
  case 3: return 13;
  case 4: return 14;
  case 5: return 15;
  case 6: return 16;
  case 7: return 17;
  default: return -1;
  }
}

int main (void) {
  const struct { fn_t a; fn_t b; } *p = &tab;
  int s = pick (3) + pick (7);
  int t = p->a (10) + p->b (20);
  printf ("s=%d t=%d\n", s, t);
  return (s == 30 && t == 33) ? 0 : 1;
}
