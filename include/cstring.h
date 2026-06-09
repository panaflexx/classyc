/* Copyright 2025 Roger Davenport */
/* MIT Licensed */

/* ============================================================================
 * cstring.h — UTF-8 aware, bounds-safe, NULL-tolerant String runtime for c2mir.
 *
 * These functions back the built-in `String` method syntax understood by the
 * c2mir compiler:
 *

  *     String s   = "Hello, this is a test";
  *     String sub = s.substr(0, 5);     ->  c2m_str_substr
  *     size_t len = s.length();         ->  c2m_str_length
  *     size_t pos = s.find("this");     ->  c2m_str_find
  *     s.replace(pos, 4, "that");       ->  c2m_str_replace
  *     if (s == null || s.empty()) ...  ->  c2m_str_empty / NULL compare
  *     String up  = s.upper();          ->  c2m_str_upper
  *     String low = s.lower();          ->  c2m_str_lower
  *
  * Static (type) methods:
  *     String s = String.copy(raw, len);  ->  c2m_str_copy
  *
  * A `String` value is represented as a NUL-terminated, heap-owned UTF-8 byte
  * buffer (a `char *`).  All offsets/lengths exposed to the user are measured in
  * Unicode code points (not bytes), so the methods behave correctly on multibyte
  * text.  Every routine is:
  *
  *   - NULL-safe   : a NULL String behaves like the empty string.
  *   - bounds-safe : positions/lengths are clamped; no out-of-range reads.
  *   - allocating  : functions that return a new String malloc a fresh buffer
  *                   (never aliasing their input) and never return NULL for a
  *                   successful-but-empty result.
  *
  * These are deliberately self-contained (no <string.h> str* dependency) so the
  * UTF-8 semantics are explicit and auditable.
  * ========================================================================== */

#ifndef C2M_CSTRING_H
#define C2M_CSTRING_H

#ifndef C2M_STR_API
#define C2M_STR_API static
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Allocation tracking / lifetime management
 * ----------------------------------------------------------------------------
 * Methods that return a fresh String (substr/replace, and the empty-string
 * helper) allocate heap memory.  Until the language gains explicit ownership
 * (e.g. a `new` keyword distinguishing heap `String *new` from stack
 * `stackString`), we track every runtime allocation in a registry so it can be
 * reclaimed deterministically instead of leaking:
 *
 *   - c2m_str_cleanup()       frees everything; auto-registered with atexit() on
 *                             the first allocation, so a program is leak-free at
 *                             normal exit with zero user effort.
 *   - c2m_str_checkpoint()    returns an opaque mark of the current allocation
 *                             count.
 *   - c2m_str_release_to(m)   frees every String allocated since mark m.
 *
 * The checkpoint/release pair is the hook a future `new`/scope model can use:
 * the compiler can emit `mark = c2m_str_checkpoint()` at the start of a scope
 * and `c2m_str_release_to(mark)` when a *stack* String goes out of scope, while
 * heap (`new`) Strings are kept alive by re-registering them above the mark (or
 * by routing them through an allocator that does not track).  For now the
 * default behavior (atexit cleanup) is always safe; scope release is opt-in.
 *
 * NOTE: the registry is a simple process-global list and is not thread-safe.
 * Generated single-threaded programs (the common case) are fine; multithreaded
 * use should guard these calls or move to per-thread registries.
 * ========================================================================== */

C2M_STR_API void **c2m__str_registry = NULL;
C2M_STR_API size_t c2m__str_reg_len = 0;
C2M_STR_API size_t c2m__str_reg_cap = 0;
C2M_STR_API int c2m__str_atexit_registered = 0;

/* Free every tracked String allocation and reset the registry. */
C2M_STR_API void c2m_str_cleanup (void) {
  size_t i;
  for (i = 0; i < c2m__str_reg_len; i++) free (c2m__str_registry[i]);
  free (c2m__str_registry);
  c2m__str_registry = NULL;
  c2m__str_reg_len = c2m__str_reg_cap = 0;
}

/* Record a heap allocation so it can be reclaimed later.  Returns p. */
C2M_STR_API void *c2m__str_track (void *p) {
  if (p == NULL) return NULL;
  if (!c2m__str_atexit_registered) {
    c2m__str_atexit_registered = 1;
    atexit (c2m_str_cleanup);
  }
  if (c2m__str_reg_len == c2m__str_reg_cap) {
    size_t ncap = c2m__str_reg_cap == 0 ? 64 : c2m__str_reg_cap * 2;
    void **n = (void **) realloc (c2m__str_registry, ncap * sizeof (void *));
    if (n == NULL) return p; /* tracking failed; still return the live pointer */
    c2m__str_registry = n;
    c2m__str_reg_cap = ncap;
  }
  c2m__str_registry[c2m__str_reg_len++] = p;
  return p;
}

/* malloc + track in one step. */
C2M_STR_API void *c2m__str_alloc (size_t n) { return c2m__str_track (malloc (n)); }

/* Opaque checkpoint of the current allocation high-water mark. */
C2M_STR_API size_t c2m_str_checkpoint (void) { return c2m__str_reg_len; }

/* Free every String allocated after checkpoint `mark`.
   Shrinks the backing array when reg_len drops well below reg_cap so a
   long-running server does not permanently hold peak allocation memory. */
C2M_STR_API void c2m_str_release_to (size_t mark) {
  size_t i;
  if (mark > c2m__str_reg_len) return;
  for (i = mark; i < c2m__str_reg_len; i++) free (c2m__str_registry[i]);
  c2m__str_reg_len = mark;
  /* Shrink the backing array when we are using < 25% of capacity. */
  if (c2m__str_reg_cap > 64 && c2m__str_reg_len < c2m__str_reg_cap / 4) {
    size_t newcap = c2m__str_reg_cap / 2;
    if (newcap < 64) newcap = 64;
    void **n = (void **) realloc (c2m__str_registry, newcap * sizeof (void *));
    if (n != NULL) { c2m__str_registry = n; c2m__str_reg_cap = newcap; }
  }
}

/* Free every String allocated after `mark` EXCEPT `keep`.  The kept pointer is
   retained and remains tracked, compacted to the front of the released range so
   that it now belongs to the enclosing scope (it survives this scope's release
   and will be reclaimed when an outer scope releases, by atexit, or by being
   kept again if returned further up).  This is what makes returning a String
   from a function safe under automatic scope reclamation.  Returns `keep`. */
C2M_STR_API void *c2m_str_release_keeping (size_t mark, void *keep) {
  size_t i, w;
  if (mark > c2m__str_reg_len) return keep;
  w = mark;
  for (i = mark; i < c2m__str_reg_len; i++) {
    if (keep != NULL && c2m__str_registry[i] == keep)
      c2m__str_registry[w++] = keep; /* retain, now owned by the enclosing scope */
    else
      free (c2m__str_registry[i]);
  }
  c2m__str_reg_len = w;
  return keep;
}

/* ---- internal byte-level primitives (no libc str* used) ---- */

/* Number of bytes in the UTF-8 sequence whose lead byte is c.
   An invalid lead byte is treated as a 1-byte sequence so iteration always
   makes forward progress (safe degradation on malformed input). */
C2M_STR_API size_t c2m__u8_seqlen (unsigned char c) {
  if (c < 0x80) return 1;          /* 0xxxxxxx */
  if ((c & 0xE0) == 0xC0) return 2; /* 110xxxxx */
  if ((c & 0xF0) == 0xE0) return 3; /* 1110xxxx */
  if ((c & 0xF8) == 0xF0) return 4; /* 11110xxx */
  return 1;                         /* invalid lead */
}

/* Byte length of a NUL-terminated string (NULL -> 0). */
C2M_STR_API size_t c2m__bytelen (const char *s) {
  size_t n = 0;
  if (s != NULL)
    while (s[n] != '\0') n++;
  return n;
}

/* Advance from s by up to `cp` code points, stopping at the terminating NUL.
   Returns a pointer into s (never NULL when s != NULL). */
C2M_STR_API const char *c2m__u8_advance (const char *s, size_t cp) {
  const unsigned char *p = (const unsigned char *) s;
  size_t i;

  for (i = 0; i < cp && *p != '\0'; i++) {
    size_t n = c2m__u8_seqlen (*p);
    size_t k;
    /* Validate continuation bytes; if truncated/invalid, advance one byte. */
    if (n > 1) {
      size_t valid = 1;
      for (k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) { valid = 0; break; }
      }
      n = valid ? n : 1;
    }
    p += n;
  }
  return (const char *) p;
}

/* Allocate and return an empty (length-0) String; never returns NULL unless OOM. */
C2M_STR_API char *c2m__empty_string (void) {
  char *r = (char *) c2m__str_alloc (1);
  if (r != NULL) r[0] = '\0';
  return r;
}

/* Copy `n` bytes from src to dst (no libc memcpy dependency). */
C2M_STR_API void c2m__copy (char *dst, const char *src, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) dst[i] = src[i];
}

/* ---- public String runtime API (imported by generated MIR code) ---- */

/* Number of Unicode code points in s (NULL -> 0). */
C2M_STR_API size_t c2m_str_length (const char *s) {
  const unsigned char *p = (const unsigned char *) s;
  size_t count = 0;

  if (s == NULL) return 0;
  while (*p != '\0') {
    size_t n = c2m__u8_seqlen (*p);
    size_t k;
    if (n > 1) {
      size_t valid = 1;
      for (k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) { valid = 0; break; }
      }
      n = valid ? n : 1;
    }
    p += n;
    count++;
  }
  return count;
}

/* 1 if s is NULL or has no bytes, else 0. */
C2M_STR_API int64_t c2m_str_empty (const char *s) {
  return (s == NULL || s[0] == '\0') ? 1 : 0;
}

/* Return a fresh String containing up to `len` code points starting at code
   point `pos`.  Out-of-range positions/lengths are clamped.  Never aliases s. */
C2M_STR_API char *c2m_str_substr (const char *s, int64_t pos, int64_t len) {
  const char *start, *end;
  size_t bytes;
  char *r;

  if (s == NULL || pos < 0 || len <= 0) return c2m__empty_string ();
  start = c2m__u8_advance (s, (size_t) pos);
  if (*start == '\0') return c2m__empty_string ();
  end = c2m__u8_advance (start, (size_t) len);
  bytes = (size_t) (end - start);
  r = (char *) c2m__str_alloc (bytes + 1);
  if (r == NULL) return NULL;
  c2m__copy (r, start, bytes);
  r[bytes] = '\0';
  return r;
}

/* Code-point index of the first occurrence of `needle` in s, or (size_t)-1 if
   not present.  Empty needle matches at index 0.  NULL-safe. */
C2M_STR_API size_t c2m_str_find (const char *s, const char *needle) {
  size_t sl, nl, i, j;

  if (s == NULL || needle == NULL) return (size_t) -1;
  nl = c2m__bytelen (needle);
  if (nl == 0) return 0;
  sl = c2m__bytelen (s);
  if (nl > sl) return (size_t) -1;

  for (i = 0; i + nl <= sl; i++) {
    for (j = 0; j < nl; j++)
      if (s[i + j] != needle[j]) break;
    if (j == nl) {
      /* Convert byte offset i to a code-point index. */
      const unsigned char *p = (const unsigned char *) s;
      size_t idx = 0, b = 0;
      while (b < i && *p != '\0') {
        size_t n = c2m__u8_seqlen (*p);
        size_t k;
        if (n > 1) {
          size_t valid = 1;
          for (k = 1; k < n; k++) {
            if ((p[k] & 0xC0) != 0x80) { valid = 0; break; }
          }
          n = valid ? n : 1;
        }
        p += n;
        b += n;
        idx++;
      }
      return idx;
    }
  }
  return (size_t) -1;
}

/* Return a fresh String equal to s with the `len` code points starting at code
   point `pos` replaced by `repl`.  Positions/lengths are clamped; a NULL repl
   acts as deletion.  Never aliases s. */
C2M_STR_API char *c2m_str_replace (const char *s, int64_t pos, int64_t len, const char *repl) {
  const char *region_start, *region_end;
  size_t pre, post, rl, total;
  char *r;

  if (s == NULL) {
    /* Treat as replacing within an empty string: result is just repl. */
    rl = c2m__bytelen (repl);
    r = (char *) c2m__str_alloc (rl + 1);
    if (r == NULL) return NULL;
    c2m__copy (r, repl == NULL ? "" : repl, rl);
    r[rl] = '\0';
    return r;
  }
  if (pos < 0) pos = 0;
  if (len < 0) len = 0;

  region_start = c2m__u8_advance (s, (size_t) pos);
  region_end = c2m__u8_advance (region_start, (size_t) len);
  pre = (size_t) (region_start - s);
  post = c2m__bytelen (region_end);
  rl = c2m__bytelen (repl);
  total = pre + rl + post;

  r = (char *) c2m__str_alloc (total + 1);
  if (r == NULL) return NULL;
  c2m__copy (r, s, pre);
  if (rl != 0) c2m__copy (r + pre, repl, rl);
  c2m__copy (r + pre + rl, region_end, post);
  r[total] = '\0';
  return r;
}

	/* Return a fresh upper-cased copy of s.  Only ASCII a-z become A-Z; all other
   bytes (including UTF-8 multi-byte sequences) are passed through unchanged.
   NULL s acts as the empty string.  Never aliases its input. */
C2M_STR_API char *c2m_str_upper (const char *s) {
  size_t n = c2m__bytelen (s);
  char *r = (char *) c2m__str_alloc (n + 1);
  size_t i;
  if (r == NULL) return NULL;
  if (s == NULL) {
    r[0] = '\0';
    return r;
  }
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char) s[i];
    if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    r[i] = (char) c;
  }
  r[n] = '\0';
  return r;
}

/* Create a fresh String by copying exactly `len` bytes from `p` (byte-oriented,
   not code-point).  Clamps negative len to 0.  NULL p or len<=0 yields the
   empty string.  The result is always NUL-terminated and tracked for
   lifetime management.  This backs the static `String.copy(raw, len)`. */
C2M_STR_API char *c2m_str_copy (const char *p, int64_t len) {
  char *r;
  size_t n;
  if (p == NULL || len <= 0) return c2m__empty_string ();
  n = (size_t) len;
  r = (char *) c2m__str_alloc (n + 1);
  if (r == NULL) return NULL;
  c2m__copy (r, p, n);
  r[n] = '\0';
  return r;
}

/* Remove `s` from the String lifetime tracker and return it as a plain char*.
   The caller takes ownership and must call free() when done.  Use this to
   store a String into external structures (stb_ds arrays without sh_new_strdup,
   struct fields, globals, etc.) that need to outlive the creating scope.
   If `s` is not found in the tracker (e.g. a string literal), it is returned
   unchanged without modification. */
C2M_STR_API char *c2m_str_detach (const char *s) {
  size_t i;
  for (i = 0; i < c2m__str_reg_len; i++) {
    if (c2m__str_registry[i] == (void *) s) {
      /* O(1) removal: swap with the last entry instead of shifting the tail. */
      c2m__str_registry[i] = c2m__str_registry[--c2m__str_reg_len];
      return (char *) s; /* caller now owns it */
    }
  }
  return (char *) s; /* not tracked (literal etc.) — safe to return as-is */
}

/* Transfer ownership of an externally-managed char* INTO the String tracker.
   After this call the pointer is treated exactly like any other tracked String:
   it will be freed automatically when the enclosing scope exits (via
   release_to / release_keeping) or at process exit via c2m_str_cleanup.
   This is the symmetric counterpart of c2m_str_detach: use it to give the
   tracker ownership of memory that was previously detached or allocated
   externally (e.g. strdup, stb_ds key retrieved before shfree, etc.).
   Returns `s` cast to char* so callers can use it as a String immediately.
   Passing NULL is a no-op that returns NULL. */
C2M_STR_API char *c2m_str_attach (const char *s) {
  if (s == NULL) return NULL;
  return (char *) c2m__str_track ((void *) s);
}

/* Return a fresh lower-cased copy of s.  Only ASCII A-Z become a-z; all other
   bytes (including UTF-8 multi-byte sequences) are passed through unchanged.
   NULL s acts as the empty string.  Never aliases its input. */
C2M_STR_API char *c2m_str_lower (const char *s) {
  size_t n = c2m__bytelen (s);
  char *r = (char *) c2m__str_alloc (n + 1);
  size_t i;
  if (r == NULL) return NULL;
  if (s == NULL) {
    r[0] = '\0';
    return r;
  }
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char) s[i];
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    r[i] = (char) c;
  }
  r[n] = '\0';
  return r;
}

/* ============================================================================
 * Predicate helpers — backs starts_with / ends_with / contains / trim methods
 * ========================================================================== */

/* Return 1 if s starts with the given prefix (byte-exact, not code-point),
   0 otherwise.  NULL or empty prefix always returns 1.  NULL s returns 0
   (unless prefix is also NULL/empty). */
C2M_STR_API int64_t c2m_str_starts_with (const char *s, const char *prefix) {
  size_t i, pl;
  if (prefix == NULL || prefix[0] == '\0') return 1;
  if (s == NULL) return 0;
  pl = c2m__bytelen (prefix);
  if (pl > c2m__bytelen (s)) return 0;
  for (i = 0; i < pl; i++)
    if (s[i] != prefix[i]) return 0;
  return 1;
}

/* Return 1 if s ends with the given suffix (byte-exact), 0 otherwise. */
C2M_STR_API int64_t c2m_str_ends_with (const char *s, const char *suffix) {
  size_t sl, sfl, i;
  const char *tail;
  if (suffix == NULL || suffix[0] == '\0') return 1;
  if (s == NULL) return 0;
  sl  = c2m__bytelen (s);
  sfl = c2m__bytelen (suffix);
  if (sfl > sl) return 0;
  tail = s + (sl - sfl);
  for (i = 0; i < sfl; i++)
    if (tail[i] != suffix[i]) return 0;
  return 1;
}

/* Return 1 if needle appears anywhere in s as a substring, 0 otherwise.
   Uses a naive byte search (appropriate for short HTTP header values and
   ASCII paths).  NULL needle or empty needle always returns 1. */
C2M_STR_API int64_t c2m_str_contains (const char *s, const char *needle) {
  size_t sl, nl, i, j;
  if (needle == NULL || needle[0] == '\0') return 1;
  if (s == NULL) return 0;
  sl = c2m__bytelen (s);
  nl = c2m__bytelen (needle);
  if (nl > sl) return 0;
  for (i = 0; i <= sl - nl; i++) {
    for (j = 0; j < nl; j++)
      if (s[i + j] != needle[j]) break;
    if (j == nl) return 1;
  }
  return 0;
}

/* Return a fresh tracked String with leading and trailing ASCII whitespace
   (any byte <= ' ') stripped.  NULL input yields the empty string. */
C2M_STR_API char *c2m_str_trim (const char *s) {
  size_t n, start, end;
  char *r;
  if (s == NULL) return c2m__empty_string ();
  n = c2m__bytelen (s);
  start = 0;
  end   = n;
  while (start < end && (unsigned char) s[start] <= ' ') start++;
  while (end > start && (unsigned char) s[end - 1] <= ' ') end--;
  if (start == end) return c2m__empty_string ();
  r = (char *) c2m__str_alloc (end - start + 1);
  if (r == NULL) return NULL;
  c2m__copy (r, s + start, end - start);
  r[end - start] = '\0';
  return r;
}

/* ============================================================================
 * basic-type -> String auto-cast helpers (String `+` concatenation extension)
 * ----------------------------------------------------------------------------
 * These back the c2mir compiler extension that lets a `String` be built with
 * the `+` operator, auto-casting basic (arithmetic) operands to text, e.g.
 *
 *     String s = "hello " + myInt + " is " + myBool;
 *
 * Every helper that returns a fresh String allocates a tracked buffer (so it is
 * reclaimed by c2m_str_release_to / atexit just like substr/replace results).
 * ========================================================================== */

/* Concatenate two Strings into a fresh tracked buffer.  A NULL operand is
   treated as the empty string.  Never aliases its inputs. */
C2M_STR_API char *c2m_str_concat (const char *a, const char *b) {
  size_t la = c2m__bytelen (a), lb = c2m__bytelen (b);
  char *r = (char *) c2m__str_alloc (la + lb + 1);
  if (r == NULL) return NULL;
  if (la != 0) c2m__copy (r, a, la);
  if (lb != 0) c2m__copy (r + la, b, lb);
  r[la + lb] = '\0';
  return r;
}

/* Render a signed 64-bit integer as a decimal String. */
C2M_STR_API char *c2m_str_from_int (int64_t v) {
  char tmp[24];
  int i = 0, neg = 0;
  uint64_t u;
  char *r;
  size_t n, k;

  if (v < 0) { neg = 1; u = (uint64_t) (-(v + 1)) + 1u; } else u = (uint64_t) v;
  do { tmp[i++] = (char) ('0' + (int) (u % 10u)); u /= 10u; } while (u != 0);
  if (neg) tmp[i++] = '-';
  n = (size_t) i;
  r = (char *) c2m__str_alloc (n + 1);
  if (r == NULL) return NULL;
  for (k = 0; k < n; k++) r[k] = tmp[i - 1 - (int) k];
  r[n] = '\0';
  return r;
}

/* Render an unsigned 64-bit integer as a decimal String. */
C2M_STR_API char *c2m_str_from_uint (uint64_t u) {
  char tmp[24];
  int i = 0;
  char *r;
  size_t n, k;

  do { tmp[i++] = (char) ('0' + (int) (u % 10u)); u /= 10u; } while (u != 0);
  n = (size_t) i;
  r = (char *) c2m__str_alloc (n + 1);
  if (r == NULL) return NULL;
  for (k = 0; k < n; k++) r[k] = tmp[i - 1 - (int) k];
  r[n] = '\0';
  return r;
}

/* Render a bool as "true"/"false". */
C2M_STR_API char *c2m_str_from_bool (int64_t v) {
  const char *s = v ? "true" : "false";
  size_t n = c2m__bytelen (s);
  char *r = (char *) c2m__str_alloc (n + 1);
  if (r == NULL) return NULL;
  c2m__copy (r, s, n);
  r[n] = '\0';
  return r;
}

/* Render a single character (low byte of c) as a 1-character String. */
C2M_STR_API char *c2m_str_from_char (int64_t c) {
  char *r = (char *) c2m__str_alloc (2);
  if (r == NULL) return NULL;
  r[0] = (char) (c & 0xff);
  r[1] = '\0';
  return r;
}

/* Render a floating-point value as a String ("%g"). */
C2M_STR_API char *c2m_str_from_double (double v) {
  char tmp[64];
  int n = snprintf (tmp, sizeof tmp, "%g", v);
  size_t len;
  char *r;

  if (n < 0) n = 0;
  len = (size_t) n < sizeof tmp ? (size_t) n : sizeof tmp - 1;
  r = (char *) c2m__str_alloc (len + 1);
  if (r == NULL) return NULL;
  c2m__copy (r, tmp, len);
  r[len] = '\0';
  return r;
}

#ifdef __cplusplus
}
#endif

#endif /* C2M_CSTRING_H */
