# ClassyC Footguns & Improvements Summary

Audited against the current compiler (2026-08-14). Original version (2026-06-23)
predates most of the fixes below; this pass re-verified every claim against
live behavior rather than carrying forward stale status. See
[`SHORTCOMINGS.md`](../cy-validate/SHORTCOMINGS.md) for the authoritative,
validated list of *current* gotchas — this document is now mostly a changelog
of what this list used to say, plus the one item still worth tracking here.

## Fixed since the original list

1. **Pointer collections needing manual cleanup** — `.owns()` exists and
   works (`List<Track*>* library = new List<Track*>(); library->owns();
   ...; delete library;` runs every element's destructor). Implemented via
   `type_kind()` classification rather than the originally-proposed
   `is_pointer<T>` intrinsic — see `COMPILER_INTRINSIC_is_pointer.md`
   (that doc's specific design is superseded; `TYPE_KIND.md` documents what
   actually shipped).
2. **`defer delete` boilerplate** — resolved, but not via the `scoped`
   keyword this doc originally proposed (that keyword was never
   implemented — it doesn't parse). Instead: `owned auto x = new Box(1);`
   already gives zero-boilerplate, LIFO, scope-exit cleanup for single heap
   objects, and by-value collections (`auto xs = List<T>();`) need no
   `new`/`delete` at all. Both shipped as part of the by-value idiom
   (`BY-VALUE.md`, `CLASSYC-CLEANUP.md`).
3. **Dict arrays** — all three sub-problems verified fixed: `d.items[0].v`
   no longer segfaults on `json()` (returns a tagged value, reads back
   correctly), `for-in` over a dict array iterates the right number of
   times (was silently 0), and `d.items.length()` exists. Matches
   `SHORTCOMINGS.md`'s C1b/C2/C3.
4. **`Filter`/`Map`/`Slice` leaking heap intermediates** — obsolete. Every
   collection transform (`Filter`/`Map`/`Take`/`Slice`/...) now returns a
   **value**, not a heap allocation — true whether the receiver is a stack
   collection or a heap `List<T>*`. There's nothing to leak between chain
   steps; only the root collection needs an owner.
5. **No warning for a missing `defer`** — implemented. The ownership
   checker now emits `warning -- leak: 'x' allocated by 'new T(...)' is
   still owned at this return but never 'delete'd...` with a fix-it hint,
   unprompted, at compile time.
6. **Track\* vs Track confusion** — addressed by the combination of
   `.owns()` existing and the README's "When you actually want pointers"
   section documenting the distinction directly.

## Still open (unchanged from original)

- **String ownership tiers are implicit** (literal / scoped-arena /
  returned / `.detach()`-ed manual) — no `OwnedString` type-level tracking
  exists to make the manual tier visibly different at the type level. The
  tiers themselves are now documented (`SHORTCOMINGS.md`, README's
  "Memory" section), which was this item's original "current workaround."
  Remains a legitimate future ergonomics idea, not a bug.

## The audit's "new finding" — since fixed

### `defer` / `owned` cleanup across a `throw` — FIXED (RAII stack dtors excepted)

The original list's item 5 ("Exception + Defer Interaction Unclear") asked
someone to go verify this and add a test; that never happened, and the
list's own "What Doesn't Need Fixing" section simultaneously (and
incorrectly) asserted `defer` "works correctly with return/throw/break."
Testing showed neither claim was right — it was a confirmed, reproducible
bug, not limited to `defer`: `throw` (`N_THROW` in `src/classyc.c`) lowers
directly to `cy_exc_throw()` + `longjmp` to the enclosing `try`'s `setjmp`
point, skipping every *syntactic* exit point where cleanup is emitted.

**Fixed (2026-08-15).** All four mechanisms now run across the jump:

- **String and object (`Any<I>`) arenas** — global mark-based stacks; the
  marks are banked into `cy_exc`'s own frame stack at try-entry and released
  on the dispatch path (fixed earlier; a register-held value is not reliably
  preserved across a setjmp/longjmp span in MIR-generated code).
- **Explicit `defer delete <class-ptr>;` and `owned` class bindings** — a
  runtime shadow stack of cleanup thunks (`cy__defer_stack` in
  `include/cyexc.h`). At check/ownership time the compiler synthesizes one
  `static void __thunk_dtor_<C>(void* p) { delete (C*)p; }` per class
  (reusing the `any<I>(C*)` erasure thunk name/registry when it already
  exists). Gen emits `cy_defer_push(thunk, ptr)` where the cleanup is
  registered — the pointer captured **by value**, Go-style — and
  `gen_run_defers` emits `cy_defer_discard_one()` for exactly the entries it
  replays on normal exits, keeping the shadow stack in sync. The try-entry
  mark banking and the dispatch-path `cy_defer_release_to()` then run every
  cleanup registered since the catching try, across any number of unwound
  call frames. Regression test: `cy-validate/val-062-defer-throw.cy`.

**Still not running across a throw:** RAII stack-object destructors
(`Point q = Point(1,2);` — the thunk arg is captured by value and a stack
address from an unwound frame is a dead-frame pointer, so this half needs
real stack unwinding), arbitrary non-`delete` defer bodies
(`defer fclose(f);`), and `delete`/`free` of non-class pointers. See
`SHORTCOMINGS.md` gotcha #9 for the exact boundaries and the capture
semantics (reassignment between `defer` and `throw`, `owned`+`move`+`throw`).

## See Also

- `SHORTCOMINGS.md` — validated, current gotchas (the doc to trust)
- `TYPE_KIND.md` — what actually shipped for pointer/value classification
- `BY-VALUE.md`, `CLASSYC-CLEANUP.md` — the by-value collection idiom that
  obsoleted several of the original footguns here
- `cy-validate/val-015-collection-byval-dtor.cy` — `__destroy` test
- `cy-validate/val-002-string-arena.cy` — String arena bound-loop test
