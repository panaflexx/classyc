# cy-validate — ClassyC README validation suite

Self-checking programs that validate the features advertised in the top-level
`README.md` against the **actual** compiler/runtime, with a heavy focus on the
**memory model** (String arena, object arena, dict arena) and edge cases in
`String`, `dict`, JSON, `Map`/`List`/`Set`, classes, exceptions, and `Any<I>`.

Discrepancies found between the README and reality (plus workarounds and one
compiler bug that was fixed) are catalogued in **[SHORTCOMINGS.md](SHORTCOMINGS.md)**.

## Running

From the project root (so `-I include` resolves). Every file is built with `-g`
(debug info — gdb-able) and JIT-run with `-eg`:

```sh
sh cy-validate/run-validate.sh
# or a single file:
./bin/classyc -g -I include cy-validate/val-006-map.cy -eg
```

Each file prints `PASS`/`FAIL` lines and exits with the number of failed
assertions (0 == all passed). The runner prints a per-file summary.

## Files

| File | Focus |
|------|-------|
| `val-001-string-methods.cy`   | String API: length/substr/find/replace/upper/lower/trim/split/join/equals + auto-cast concat |
| `val-002-string-arena.cy`     | **Memory:** automatic String arena (release_keeping on return; 200k-alloc loop stays bounded) |
| `val-003-dict.cy`             | dict: init/nested/dot/subscript/dynamic keys/`in`/for-in/json round-trip/`.json` |
| `val-004-dict-arrays.cy`      | dict JSON arrays: `d.arr[i]`, `d.items[0].name`, `(int)d.items[0].value`, documented limits |
| `val-005-dict-arena.cy`       | **Memory:** `new dict(bytes)` / `new dict()` / `delete` / `defer delete`; 2000-arena churn |
| `val-006-map.cy`              | Map<K,V>: subscript, for-in, KeyAt/ValAt, Copy/Merge, object values, 5k-entry growth |
| `val-007-list.cy`             | List<T>: brace-init, Filter/Sort/ForEach/Slice/Copy, array `.ToList()`, slice pipeline |
| `val-008-set.cy`              | Set<T>: **content** hashing for String vs **identity** hashing for objects |
| `val-009-classes.cy`          | ctor/dtor, new/delete, fluent chaining, named args, defer destructor LIFO ordering |
| `val-010-exceptions.cy`       | try/catch/throw, multi-catch, user enum exceptions, **default-on** safety guards |
| `val-011-fstring-auto.cy`     | f-strings (vars + expressions) and `auto` disambiguation (int/array/dict) |
| `val-012-interfaces-any.cy`   | interface/impl, structural conformance, Any<I> erased dispatch, recursive delete |
| `val-013-any-edge.cy`         | Any<I> rough edges: non-void/arg methods, pass/return handles, Map/List of Any, composition, same class erased to two interfaces (E1 fix) |
| `val-014-any-return-mem.cy`   | **Memory:** regression test for the fixed object-arena return-handle use-after-free |
| `val-015-string-literal-and-replace.cy` | String methods on a string-literal receiver (B1), 2-arg `replace(needle,repl)` search-and-replace (A2), and `List<T>.Map` (B4) |
| `val-022-owned-move-readonly.cy` | **Memory:** managed-ownership layer — `owned` single-owner auto-release at scope exit, `move` ownership transfer (source → read-only view, no double free), chained moves, `readonly` non-owning borrow |

## Headline findings

- `-fexceptions` and the JIT safety guards are **ON by default** (README said
  the opposite). Validated in `val-010`.
- Several README snippets didn't compile/run as written (`String.checkpoint()`,
  `printf("%s", dict_value)`). See SHORTCOMINGS.md A/B.
- **Ergonomics fixed:** String methods now work directly on a string literal
  (`"abc".lower()`, B1), `replace(needle, repl)` is search-and-replace (A2), and
  `List<T>.Map` exists and chains with `Filter` (B4). See `val-015`.
- dict JSON arrays work at the value level (`d.arr[i]`); for-in over an array
  value and treating a numeric leaf as a dict do not. See SHORTCOMINGS.md C.
- **Compiler bug found & fixed:** returning an `Any<I>` handle from a function
  was a use-after-free; now the handle is detached to the caller. See
  SHORTCOMINGS.md E0 and `val-014`.
- **Compiler bug found & fixed:** erasing the *same* class to two different
  interfaces aborted codegen (`Repeated item declaration __thunk_dtor_<Class>`);
  the per-class forwarding/destructor thunks are now emitted once and
  re-declared on later erasures. See SHORTCOMINGS.md E1 and `val-013`.
