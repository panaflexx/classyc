#ifndef C2M_COBJARENA_H
#define C2M_COBJARENA_H

/* ============================================================================
 * cobjarena.h — scope-bound object arena for ClassyC collections.
 *
 * A sibling of the String registry in cstring.h, but for heap *objects* that
 * carry a destructor (notably the compiler-synthesized Any<I> handles).  Each
 * tracked entry stores a pointer plus an optional destructor thunk; releasing a
 * scope runs the destructor (which frees the wrapped concrete object) and then
 * the entry is dropped — exactly the "collections are arena managed" model:
 *
 *   - c2m_obj_track(p, dtor)  register a heap object and how to destroy it.
 *                             dtor == NULL means "just free(p)".
 *   - c2m_obj_checkpoint()    opaque mark of the current high-water count.
 *   - c2m_obj_release_to(m)   destroy + drop every object tracked since mark m,
 *                             in reverse (LIFO) order so destructor ordering is
 *                             the natural inverse of construction.
 *
 * The compiler emits a checkpoint at the entry of a scope that constructs Any<I>
 * handles and a release_to at every exit, so handles placed into collections are
 * reclaimed automatically when the scope ends.  An atexit() net guarantees a
 * leak-free normal exit with zero user effort.
 *
 * NOTE: the registry is PER-THREAD (C11 _Thread_local), mirroring the String
 * registry: each OS thread owns an independent LIFO stack, so checkpoint /
 * release_to need no locks.  An object tracked on thread A must not be
 * released on thread B — hand it across with c2m_obj_detach (A) +
 * c2m_obj_track (B).  Main-thread leftovers are swept at exit via atexit();
 * spawned-thread leftovers are swept by a pthread TSD destructor at thread
 * exit (define C2M_OBJ_NO_PTHREAD to opt out).
 * ========================================================================== */

#include <stdlib.h>

#ifndef C2M_OBJ_NO_PTHREAD
#include <pthread.h>
#endif

#ifndef C2M_OBJ_API
#define C2M_OBJ_API static
#endif

typedef void (*c2m_obj_dtor_t) (void *);

typedef struct {
  void *ptr;
  c2m_obj_dtor_t dtor;
} c2m__obj_entry;

C2M_OBJ_API _Thread_local c2m__obj_entry *c2m__obj_registry = NULL;
C2M_OBJ_API _Thread_local size_t c2m__obj_reg_len = 0;
C2M_OBJ_API _Thread_local size_t c2m__obj_reg_cap = 0;
C2M_OBJ_API int c2m__obj_atexit_registered = 0;

#ifndef C2M_OBJ_NO_PTHREAD
C2M_OBJ_API pthread_key_t c2m__obj_tls_key;
C2M_OBJ_API pthread_once_t c2m__obj_tls_once = PTHREAD_ONCE_INIT;
C2M_OBJ_API void c2m_obj_cleanup (void);
C2M_OBJ_API void c2m__obj_tls_dtor (void *p) {
  (void) p;
  c2m_obj_cleanup ();
  (void) pthread_setspecific (c2m__obj_tls_key, NULL); /* disarm: no re-run */
}
C2M_OBJ_API void c2m__obj_tls_init (void) {
  (void) pthread_key_create (&c2m__obj_tls_key, c2m__obj_tls_dtor);
}
#endif

/* Destroy and drop a single entry (dtor owns the free; NULL dtor => plain free). */
C2M_OBJ_API void c2m__obj_destroy_one (c2m__obj_entry e) {
  if (e.ptr == NULL) return;
  if (e.dtor != NULL)
    e.dtor (e.ptr);
  else
    free (e.ptr);
}

/* Destroy every tracked object (LIFO) and reset the registry.  The length is
   reset BEFORE running destructors so a destructor that re-enters the arena
   (e.g. the Any<I> free thunk calls `delete`, which detaches) sees a consistent
   registry and cannot disturb the in-progress sweep. */
C2M_OBJ_API void c2m_obj_cleanup (void) {
  size_t old_len = c2m__obj_reg_len, i;
  c2m__obj_reg_len = 0;
  for (i = old_len; i-- > 0;) c2m__obj_destroy_one (c2m__obj_registry[i]);
  free (c2m__obj_registry);
  c2m__obj_registry = NULL;
  c2m__obj_reg_len = c2m__obj_reg_cap = 0;
}

/* Record a heap object plus its destructor thunk so it can be reclaimed later.
   Returns p so call sites can wrap an allocation inline. */
C2M_OBJ_API void *c2m_obj_track (void *p, c2m_obj_dtor_t dtor) {
  if (p == NULL) return NULL;
  if (!c2m__obj_atexit_registered) {
    c2m__obj_atexit_registered = 1;
    atexit (c2m_obj_cleanup); /* sweeps the calling (main) thread's registry */
  }
#ifndef C2M_OBJ_NO_PTHREAD
  (void) pthread_once (&c2m__obj_tls_once, c2m__obj_tls_init);
  (void) pthread_setspecific (c2m__obj_tls_key, (void *) 1);
#endif
  if (c2m__obj_reg_len == c2m__obj_reg_cap) {
    size_t ncap = c2m__obj_reg_cap == 0 ? 64 : c2m__obj_reg_cap * 2;
    c2m__obj_entry *n
      = (c2m__obj_entry *) realloc (c2m__obj_registry, ncap * sizeof (c2m__obj_entry));
    if (n == NULL) return p; /* tracking failed; still return the live pointer */
    c2m__obj_registry = n;
    c2m__obj_reg_cap = ncap;
  }
  c2m__obj_registry[c2m__obj_reg_len].ptr = p;
  c2m__obj_registry[c2m__obj_reg_len].dtor = dtor;
  c2m__obj_reg_len++;
  return p;
}

/* Opaque checkpoint of the current high-water count. */
C2M_OBJ_API size_t c2m_obj_checkpoint (void) { return c2m__obj_reg_len; }

/* Destroy + drop every object tracked after checkpoint `mark`, in LIFO order.
   Truncates to `mark` BEFORE running destructors (see c2m_obj_cleanup) so a
   re-entrant detach/track from within a destructor stays consistent. */
C2M_OBJ_API void c2m_obj_release_to (size_t mark) {
  size_t old_len, i;
  if (mark > c2m__obj_reg_len) return;
  old_len = c2m__obj_reg_len;
  c2m__obj_reg_len = mark;
  for (i = old_len; i-- > mark;) c2m__obj_destroy_one (c2m__obj_registry[i]);
  if (c2m__obj_reg_cap > 64 && c2m__obj_reg_len < c2m__obj_reg_cap / 4) {
    size_t newcap = c2m__obj_reg_cap / 2;
    if (newcap < 64) newcap = 64;
    c2m__obj_entry *n
      = (c2m__obj_entry *) realloc (c2m__obj_registry, newcap * sizeof (c2m__obj_entry));
    if (n != NULL) { c2m__obj_registry = n; c2m__obj_reg_cap = newcap; }
  }
}

/* Untrack a specific pointer without destroying it (hand ownership back to the
   caller, e.g. when an Any handle is explicitly `delete`d or escapes its scope).
   Returns p.  O(n) scan from the most-recent end (handles are usually recent). */
C2M_OBJ_API void *c2m_obj_detach (void *p) {
  size_t i = c2m__obj_reg_len;
  while (i-- > 0) {
    if (c2m__obj_registry[i].ptr == p) {
      c2m__obj_registry[i] = c2m__obj_registry[--c2m__obj_reg_len];
      return p;
    }
  }
  return p;
}

#endif /* C2M_COBJARENA_H */
