# Select stack probes (P1 evidence)

Run from project root:

```sh
for f in sketch/probe-select-*.cy; do
  printf '%-40s ' "$(basename "$f")"
  if ./bin/classyc -g -I include "$f" -eg >/tmp/ps.out 2>/tmp/ps.err; then
    echo PASS
  else
    echo FAIL
  fi
done
```

| File | Expect today | After P1 |
|------|--------------|----------|
| `probe-select-heap.cy` | PASS | PASS |
| `probe-select-heap-ship.cy` | PASS | PASS |
| `probe-select-after-new.cy` | PASS (heap primes stack) | PASS |
| `probe-select-seedfn.cy` | PASS (`List*` param primes) | PASS |
| `probe-select-lapsample-seed.cy` | PASS | PASS |
| `probe-select-ptr.cy` | FAIL pure stack | **PASS** |
| `probe-select-ship.cy` | FAIL | **PASS** |
| `probe-select-lapsample.cy` | FAIL | **PASS** |
| `probe-select-after-where.cy` | FAIL | **PASS** |
| `probe-select-warm-copy.cy` | FAIL | **PASS** |
| `probe-select-aurora-min.cy` | FAIL | **PASS** |
| `probe-select-aurora-order.cy` | FAIL | **PASS** |
| `probe-select-shared-spec.cy` | PASS (warm new) | PASS |

Root cause: method-generic `Select` monomorph attaches when `List<T>*` is
seen first; pure value `List<T>` specializations often omit it.
