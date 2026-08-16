# P2 sketch — uncaught exception / safety trap: exit(1), not abort

## Today

`include/cyexc.h`:

```c
C2M_EXC_API void cy_exc_throw(...) {
  ...
  if (cy__exc_depth > 0) longjmp(...);
  fprintf(stderr, "uncaught exception %u: %s", id, msg ? msg : "(no message)");
  if (file != NULL) fprintf(stderr, " at %s:%d", file, line);
  fprintf(stderr, "\n");
  abort();   /* ← core dump; breaks bugs/004 UX */
}

C2M_EXC_API void _safety_trap(long reason, long file_id, long line) {
  ...
  if (cy_exc_active()) { cy_exc_throw(...); return; }
  fprintf(stderr, "fatal: %s (line %ld)\n", what, line);
  abort();
}
```

## Target

```c
  fprintf(stderr, "uncaught exception %u: %s", ...);
  ...
#if defined(CY_EXC_ABORT) && CY_EXC_ABORT
  abort();
#else
  exit(1);   /* or _Exit(1) to skip atexit if desired */
#endif
```

Same for `_safety_trap` uncaught branch.

## Why useful

* OOB List/Map, null, div0 already throw; uncaught should be a clean process
  failure for scripts/CI, not a core file by default.  
* Aligns FINDINGS note on bugs/004.  
* Still prints id/msg/file/line.

## Validation

```c
// bugs/004 or new val: expect exit 1, message on stderr, no core
List<int>* a = new List<int>();
a->Get(0);   // throws OutOfBounds, uncaught
```

```sh
./bin/classyc -I include t.cy -eg; echo exit:$?   # expect 1
```

## Effort

S — half day including dual paths in cyexc + any AOT runtime copy of the same
helpers (`mir-aot-runtime.c` if it embeds a copy).
