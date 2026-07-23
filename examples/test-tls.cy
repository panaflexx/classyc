/* test-tls.cy — issue #394 semantics for ClassyC `_Thread_local`
 *
 * Child thread sets TLS; main must still see its own value.
 * Addresses of TLS vars must differ across pthreads.
 *
 *   ./bin/classyc examples/test-tls.cy -eg
 *   ./classyc-aot.sh examples/test-tls.cy -o /tmp/test-tls && /tmp/test-tls
 *
 * @expect: exit 0, print "TLS OK"
 */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>

static _Thread_local int x;
static _Thread_local int y = 7;

static void *thread_fn (void *arg) {
  (void) arg;
  assert (x == 0);
  assert (y == 7);
  x = 1;
  y = 99;
  return (void *) (uintptr_t) &x;
}

int main (void) {
  x = 0;
  assert (y == 7);

  pthread_t thread;
  int err = pthread_create (&thread, NULL, thread_fn, NULL);
  assert (err == 0);
  void *ret = NULL;
  err = pthread_join (thread, &ret);
  assert (err == 0);
  assert (ret != NULL);

  /* Main still has original TLS values (issue #394). */
  assert (x == 0);
  assert (y == 7);
  /* Distinct cell addresses. */
  assert ((void *) &x != ret);

  printf ("TLS OK (main_x=%p child_x=%p y=%d)\n", (void *) &x, ret, y);
  return 0;
}
