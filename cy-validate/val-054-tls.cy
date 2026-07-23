/* val-054-tls.cy — C11 `_Thread_local` per-OS-thread storage
 *
 * @expect: exit 0
 */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

static _Thread_local int tls_x;
static _Thread_local int tls_y = 42;

static void *worker (void *arg) {
  int id = (int) (intptr_t) arg;
  assert (tls_x == 0);
  assert (tls_y == 42);
  tls_x = id;
  tls_y = id * 10;
  return (void *) (intptr_t) tls_x;
}

int main (void) {
  tls_x = 100;
  tls_y = 200;

  pthread_t t1, t2;
  assert (pthread_create (&t1, NULL, worker, (void *) (intptr_t) 1) == 0);
  assert (pthread_create (&t2, NULL, worker, (void *) (intptr_t) 2) == 0);

  void *r1 = NULL, *r2 = NULL;
  assert (pthread_join (t1, &r1) == 0);
  assert (pthread_join (t2, &r2) == 0);
  assert ((intptr_t) r1 == 1);
  assert ((intptr_t) r2 == 2);

  /* Main TLS unchanged. */
  assert (tls_x == 100);
  assert (tls_y == 200);

  printf ("val-054-tls: OK\n");
  return 0;
}
