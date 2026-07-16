/* Copyright 2025 Roger Davenport */
/* MIT Licensed */

/* ============================================================================
 * cyexc.h — try/catch/throw runtime for ClassyC.
 *
 * This is a *compiler-internal* runtime (like cstring.h / dict.h): user `.cy`
 * programs never include it.  The code generator emits imports of these
 * functions by name, and they are resolved at JIT time through the driver's
 * import resolver and at AOT time by linking mir-aot-runtime.c.
 *
 * Model (setjmp/longjmp, single-threaded):
 *
 *     buf = cy_exc_push();           // push a frame, get its jmp_buf
 *     if (setjmp(buf) != 0) ...      // emitted inline in the try function
 *     ... protected body ...
 *     cy_exc_pop();                  // unwind one frame on normal exit
 *
 *     throw(id, msg)  ->  cy_exc_throw(id, msg, file, line);  // records + longjmp
 *
 * `cy_exception_t` mirrors the `Exception` value type injected by the compiler
 * prelude (id, msg, file, line); a `catch` variable is populated by copying
 * *cy_exc_current() into it.
 *
 * Limitations (v1): single global frame stack (not thread-safe); a `return`,
 * `break`, or `continue` that jumps out of a `try` body does not pop its frame,
 * so avoid those until frame unwinding is integrated with `defer`.
 * ========================================================================== */

#ifndef C2M_CYEXC_H
#define C2M_CYEXC_H

#ifndef C2M_EXC_API
#define C2M_EXC_API static
#endif

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must match the `Exception` typedef injected by the compiler prelude. */
typedef struct {
  unsigned int id;
  const char *msg;
  const char *file;
  int line;
} cy_exception_t;

#ifndef CY_EXC_MAX_DEPTH
#define CY_EXC_MAX_DEPTH 256
#endif

/* Built-in exception class ids — mirror the prelude enum in classyc.c. */
#define CY_EXC_ANY                0
#define CY_EXC_NULL               1
#define CY_EXC_OUT_OF_BOUNDS      2
#define CY_EXC_ARITHMETIC         3
#define CY_EXC_RUNTIME            4

static jmp_buf        cy__exc_frames[CY_EXC_MAX_DEPTH];
static int            cy__exc_depth = 0;
static cy_exception_t cy__exc_current = {0, 0, 0, 0};

/* Push a new frame; return a pointer to its jmp_buf for the inline setjmp(). */
C2M_EXC_API void *cy_exc_push (void) {
  if (cy__exc_depth >= CY_EXC_MAX_DEPTH) {
    fprintf (stderr, "cy_exc: try nesting too deep (> %d)\n", CY_EXC_MAX_DEPTH);
    abort ();
  }
  return (void *) &cy__exc_frames[cy__exc_depth++];
}

/* Unwind one frame (on normal try completion, or after entering a handler). */
C2M_EXC_API void cy_exc_pop (void) {
  if (cy__exc_depth > 0) cy__exc_depth--;
}

/* Pointer to the current / last-thrown exception record. */
C2M_EXC_API void *cy_exc_current (void) {
  return (void *) &cy__exc_current;
}

/* Is there an active try frame?  (Used by safety traps: throw vs. abort.) */
C2M_EXC_API int cy_exc_active (void) {
  return cy__exc_depth > 0;
}

/* Record an exception and longjmp to the innermost frame.  Never returns: if no
   frame is active the exception is uncaught, so report and exit(1).
   Define CY_EXC_ABORT=1 to keep abort() (core dump) for debugger sessions. */
C2M_EXC_API void cy_exc_throw (unsigned int id, const char *msg, const char *file, int line) {
  cy__exc_current.id = id;
  cy__exc_current.msg = msg;
  cy__exc_current.file = file;
  cy__exc_current.line = line;
  if (cy__exc_depth > 0) longjmp (cy__exc_frames[cy__exc_depth - 1], 1);
  fprintf (stderr, "uncaught exception %u: %s", id, msg ? msg : "(no message)");
  if (file != NULL) fprintf (stderr, " at %s:%d", file, line);
  fprintf (stderr, "\n");
#if defined(CY_EXC_ABORT) && CY_EXC_ABORT
  abort ();
#else
  exit (1);
#endif
}

/* JIT safety trap (bounds/null/div-zero/shift guards, gated on -fexceptions).
   Converts a detected fault into a catchable exception when inside a try block,
   or exits with a diagnostic when uncaught.
   reason: 1=OOB, 2=null-ptr, 3=div-by-zero, 4=use-after-free, 5=shift-out-of-range. */
C2M_EXC_API void _safety_trap (long reason, long file_id, long line) {
  static const char *const reasons[]
    = {"unknown fault", "out-of-bounds index", "null-pointer dereference",
       "division by zero", "use-after-free", "shift out of range"};
  long n = (long) (sizeof (reasons) / sizeof (reasons[0]));
  const char *what = (reason >= 0 && reason < n) ? reasons[reason] : "unknown fault";
  unsigned int id = reason == 1 ? CY_EXC_OUT_OF_BOUNDS
                    : reason == 2 ? CY_EXC_NULL
                    : reason == 3 ? CY_EXC_ARITHMETIC
                    : reason == 5 ? CY_EXC_ARITHMETIC
                                  : CY_EXC_RUNTIME;
  (void) file_id;
  if (cy_exc_active ()) cy_exc_throw (id, what, NULL, (int) line);
  fprintf (stderr, "fatal: %s (line %ld)\n", what, line);
#if defined(CY_EXC_ABORT) && CY_EXC_ABORT
  abort ();
#else
  exit (1);
#endif
}

#ifdef __cplusplus
}
#endif

/* ── Safe allocator / free wrapper ────────────────────────────────────────────
   cy_safe_alloc / cy_safe_free / cy_safe_deref are called by the compiler's
   gen stage when -fexceptions is active:

     cy_safe_alloc(size) — plain malloc; OOM is caught by the inline
                           gen_oom_check emitted right after the call.

     cy_safe_free(ptr, line) — throws RuntimeException on delete of null.
                           The codegen also nulls out the deleted pointer
                           variable so that same-variable double-free and
                           use-after-free become null-pointer dereferences
                           (caught by the null guard).

     cy_safe_deref(ptr, line) — available for manual use in user code;
                           not auto-emitted because, without intercepting
                           ALL malloc calls (including runtime-internal
                           ones like List<String>* from split()), a
                           per-deref registry check produces false positives
                           when non-tracked allocations reuse freed addresses.

   Alias double-free/UAF detection requires a full malloc intercept (ASAN
   or similar); the above covers the common same-variable cases reliably. */

/* Allocate size bytes.  Returns NULL on OOM (caller checks via gen_oom_check). */
C2M_EXC_API void *cy_safe_alloc (uint64_t size) {
  return malloc ((size_t) size);
}

/* Free ptr.  Throws on null (catches same-variable double-free after null-out). */
C2M_EXC_API void cy_safe_free (void *ptr, long line) {
  if (ptr == NULL) {
    cy_exc_throw (CY_EXC_RUNTIME, "delete of null pointer (double-free?)", NULL, (int) line);
    return;
  }
  free (ptr);
}

/* Manual use-after-free guard (not auto-emitted; see note above). */
C2M_EXC_API void cy_safe_deref (void *ptr, long line) {
  (void) ptr; (void) line; /* no-op without a global malloc intercept */
}

/* ── Object guards: side-table + quarantine (opt-in via -fobject-guards) ──────
 *
 * A layout-preserving use-after-free / double-free detector for `new` class
 * objects.  Unlike a header-prefix "tagged allocator" (which changes the
 * pointer the program holds and breaks every free path / C interop), this
 * keeps `new` a plain malloc and tracks liveness in a SIDE TABLE keyed by the
 * object address.  The compiler emits, under -fobject-guards:
 *
 *     new C(...)        ->  ...malloc..., cy_obj_track(p)
 *     delete p          ->  ...dtor..., cy_obj_note_free(p, line)   (no free!)
 *     p->field  (CHECK) ->  cy_obj_check(p, line); ...access...
 *
 * where CHECK sites are exactly the derefs the ownership pass could not prove
 * live (state MaybeOwned).  Proven-live (OWNED) derefs are never instrumented.
 *
 * Freed blocks are QUARANTINED, not returned to malloc, so their addresses are
 * never reused while still tracked -- this is what makes the check free of the
 * false positives the cy_safe_deref note above describes (a reused address can
 * never masquerade as a live-then-dead tracked object).  When the quarantine
 * ring is full the oldest block is really free()d AND its table entry removed,
 * so a much-later deref of that stale pointer finds no entry and is treated
 * leniently (no throw) rather than a false positive.
 *
 * Single-threaded (like the rest of cyexc); wrap the table in a mutex or make
 * it _Thread_local before using guards in threaded code. */

#ifndef CY_OBJ_TAB_BITS
#define CY_OBJ_TAB_BITS 16               /* 65536 slots (open addressing) */
#endif
#define CY_OBJ_TAB_SIZE (1u << CY_OBJ_TAB_BITS)
#define CY_OBJ_TAB_MASK (CY_OBJ_TAB_SIZE - 1u)

#ifndef CY_OBJ_QUARANTINE
#define CY_OBJ_QUARANTINE 4096           /* freed blocks held before real free() */
#endif

#define CY_OBJ_EMPTY 0                   /* slot unused                          */
#define CY_OBJ_LIVE  1                   /* address currently owns a live object */
#define CY_OBJ_DEAD  2                   /* address freed (quarantined)          */

typedef struct { uintptr_t addr; unsigned char state; } cy_obj_slot_t;
static cy_obj_slot_t cy__obj_tab[CY_OBJ_TAB_SIZE];
static void         *cy__obj_quar[CY_OBJ_QUARANTINE];
static size_t        cy__obj_quar_head = 0;
static size_t        cy__obj_quar_count = 0;

static size_t cy__obj_hash (uintptr_t a) {
  /* Fibonacci hash of the address (drop the low zero bits from alignment). */
  a >>= 4;
  return (size_t) ((a * 11400714819323198485ull) >> (64 - CY_OBJ_TAB_BITS)) & CY_OBJ_TAB_MASK;
}

/* Find the slot for `a`: returns index of its entry or of the first EMPTY slot
   on the probe chain (insertion point).  Table is sized generously and never
   grows; if it fills, we degrade to lenient (treat as untracked). */
static size_t cy__obj_find (uintptr_t a) {
  size_t i = cy__obj_hash (a);
  for (size_t n = 0; n < CY_OBJ_TAB_SIZE; n++) {
    if (cy__obj_tab[i].state == CY_OBJ_EMPTY || cy__obj_tab[i].addr == a) return i;
    i = (i + 1) & CY_OBJ_TAB_MASK;
  }
  return cy__obj_hash (a); /* full: caller overwrites, worst case a stale entry */
}

/* Register a freshly `new`-allocated object as live. */
C2M_EXC_API void cy_obj_track (void *ptr) {
  if (ptr == NULL) return;
  uintptr_t a = (uintptr_t) ptr;
  size_t i = cy__obj_find (a);
  cy__obj_tab[i].addr = a;
  cy__obj_tab[i].state = CY_OBJ_LIVE;
}

/* Record that `ptr` is being deleted.  Throws on double-free.  Does NOT free
   the block: it is quarantined so its address can't be reused while tracked.
   The oldest quarantined block is really free()d (and untracked) when the ring
   overflows. */
C2M_EXC_API void cy_obj_note_free (void *ptr, long line) {
  if (ptr == NULL) return; /* delete of null is a no-op elsewhere */
  uintptr_t a = (uintptr_t) ptr;
  size_t i = cy__obj_find (a);
  if (cy__obj_tab[i].state == CY_OBJ_DEAD && cy__obj_tab[i].addr == a) {
    cy_exc_throw (CY_EXC_RUNTIME, "double free of object", NULL, (int) line);
    return;
  }
  /* Mark dead (track it even if it wasn't seen at alloc: still catches a later
     double free). */
  cy__obj_tab[i].addr = a;
  cy__obj_tab[i].state = CY_OBJ_DEAD;

  /* Quarantine ring: evict + really free the oldest when full. */
  if (cy__obj_quar_count == CY_OBJ_QUARANTINE) {
    void *old = cy__obj_quar[cy__obj_quar_head];
    size_t j = cy__obj_find ((uintptr_t) old);
    if (cy__obj_tab[j].addr == (uintptr_t) old) cy__obj_tab[j].state = CY_OBJ_EMPTY;
    free (old);
    cy__obj_quar[cy__obj_quar_head] = ptr;
    cy__obj_quar_head = (cy__obj_quar_head + 1) % CY_OBJ_QUARANTINE;
  } else {
    cy__obj_quar[(cy__obj_quar_head + cy__obj_quar_count) % CY_OBJ_QUARANTINE] = ptr;
    cy__obj_quar_count++;
  }
}

/* Liveness check before a dereference the compiler couldn't prove live.
   Throws use-after-free if the address is a known-dead (quarantined) object.
   A live or untracked address passes (untracked => lenient, no false positive). */
C2M_EXC_API void cy_obj_check (void *ptr, long line) {
  if (ptr == NULL) return; /* null is handled by the separate null guard */
  uintptr_t a = (uintptr_t) ptr;
  size_t i = cy__obj_find (a);
  if (cy__obj_tab[i].state == CY_OBJ_DEAD && cy__obj_tab[i].addr == a)
    cy_exc_throw (CY_EXC_RUNTIME, "use-after-free", NULL, (int) line);
}

#endif /* C2M_CYEXC_H */
