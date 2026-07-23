/* val-055-tls-arena.cy — per-thread String/object arenas + cross-thread attach
 *
 * The String registry (cstring.h) and object registry (cobjarena.h) are
 * _Thread_local: every OS thread owns an independent positional arena stack.
 *
 *   1. A spawned thread starts with an EMPTY arena (checkpoint == 0) even
 *      though the main thread holds live tracked Strings.
 *   2. Allocations on a worker never move the main thread's high-water mark.
 *   3. Cross-thread handoff works: main detach()es a String, the worker
 *      attach()es it — it is tracked in the WORKER's arena and freed there,
 *      exactly once.
 *
 * @expect: exit 0
 */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern size_t c2m_str_checkpoint (void);
extern void   c2m_str_release_to (size_t mark);
extern char  *c2m_str_detach (const char *s);
extern char  *c2m_str_attach (const char *s);
extern size_t c2m_obj_checkpoint (void);

/* Churn worker: allocate heavily; per-scope / per-iteration release must
   keep this thread's arena flat, and other threads must not be disturbed. */
static void *churn_worker (void *arg) {
  intptr_t id = (intptr_t) arg;
  assert (c2m_str_checkpoint () == 0);  /* fresh thread → empty arena */
  assert (c2m_obj_checkpoint () == 0);

  for (int i = 0; i < 200; i++) {
    String a = f"thr{id}-item-{i}";
    String b = a.upper ();
    assert (b.length () > 0);
    assert (c2m_str_checkpoint () >= 2);   /* live allocations on THIS thread */
  }
  assert (c2m_str_checkpoint () == 0);     /* loop-body scope release fired */
  return NULL;
}

/* Handoff worker: receive a malloc-owned buffer detached by main, attach it
   into THIS thread's arena, verify contents, and let scope release free it. */
static void *handoff_worker (void *arg) {
  char *raw = (char *) arg;
  assert (c2m_str_checkpoint () == 0);
  assert (strcmp (raw, "handoff-payload") == 0); /* data survived the crossing */

  String s = c2m_str_attach (raw);
  assert (c2m_str_checkpoint () == 1);           /* tracked HERE, not in main */
  assert (strcmp ((char *) s, "handoff-payload") == 0);

  c2m_str_release_to (0);                        /* freed exactly once, here */
  assert (c2m_str_checkpoint () == 0);
  return NULL;
}

int main (void) {
  String keep = "main-keep".substr (0, 9); /* tracked copy in main's arena */
  size_t m0 = c2m_str_checkpoint ();
  assert (m0 >= 1);
  size_t om0 = c2m_obj_checkpoint ();

  /* Detach the handoff payload BEFORE crossing: main's arena no longer
     owns it, so the worker's arena can take sole ownership. */
  String h = "handoff-payload".substr (0, 15); /* tracked (non-literal) */
  char *raw = c2m_str_detach ((char *) h);
  assert (c2m_str_checkpoint () == m0);

  pthread_t t1, t2, t3;
  assert (pthread_create (&t1, NULL, churn_worker, (void *) (intptr_t) 1) == 0);
  assert (pthread_create (&t2, NULL, churn_worker, (void *) (intptr_t) 2) == 0);
  assert (pthread_create (&t3, NULL, handoff_worker, raw) == 0);
  assert (pthread_join (t1, NULL) == 0);
  assert (pthread_join (t2, NULL) == 0);
  assert (pthread_join (t3, NULL) == 0);

  /* Workers churned hundreds of Strings: main's marks never moved. */
  assert (c2m_str_checkpoint () == m0);
  assert (c2m_obj_checkpoint () == om0);
  assert (strcmp ((char *) keep, "main-keep") == 0);

  printf ("val-055-tls-arena: OK\n");
  return 0;
}
