/* val-051: MIR atomics via _Atomic + <stdatomic.h> (seq_cst). */
#include <stdio.h>
#include <stdatomic.h>

static _Atomic int g_counter;
static atomic_long g_flag;

int main (void) {
  int old, ok;
  long exp, got;

  g_counter = 0;
  atomic_store (&g_counter, 10);
  old = atomic_load (&g_counter);
  if (old != 10) {
    printf ("FAIL load/store %d\n", old);
    return 1;
  }

  old = atomic_fetch_add (&g_counter, 1);
  if (old != 10 || atomic_load (&g_counter) != 11) {
    printf ("FAIL fetch_add %d / %d\n", old, atomic_load (&g_counter));
    return 1;
  }

  g_counter += 4;
  if (atomic_load (&g_counter) != 15) {
    printf ("FAIL += %d\n", atomic_load (&g_counter));
    return 1;
  }

  ++g_counter;
  if (g_counter != 16) {
    printf ("FAIL ++ %d\n", g_counter);
    return 1;
  }

  old = g_counter++;
  if (old != 16 || g_counter != 17) {
    printf ("FAIL post++ %d / %d\n", old, g_counter);
    return 1;
  }

  g_flag = 0;
  exp = 0;
  ok = atomic_compare_exchange_strong (&g_flag, &exp, 42);
  if (!ok || g_flag != 42 || exp != 0) {
    printf ("FAIL cas success ok=%d flag=%ld exp=%ld\n", ok, g_flag, exp);
    return 1;
  }

  exp = 0;
  ok = atomic_compare_exchange_strong (&g_flag, &exp, 99);
  if (ok || g_flag != 42 || exp != 42) {
    printf ("FAIL cas fail ok=%d flag=%ld exp=%ld\n", ok, g_flag, exp);
    return 1;
  }

  got = atomic_exchange (&g_flag, 7);
  if (got != 42 || g_flag != 7) {
    printf ("FAIL exchange %ld / %ld\n", got, g_flag);
    return 1;
  }

  atomic_thread_fence (memory_order_seq_cst);
  printf ("atomics OK\n");
  return 0;
}
