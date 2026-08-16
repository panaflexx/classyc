# P5 sketch — capturing lambdas for Find / FindOr

## Today

`get_hof_kind` recognizes `Find`, but List open-code hits:

```c
} else if (hk == HOF_FIND) {
  error(..., "capturing lambda in Find is not supported yet "
             "(use Where/Any or a non-capturing pred)");
  return FALSE;
}
```

Non-capturing `Find` still works (thin fn ptr).  
Users need globals for thresholds:

```c
int g_want;
int is_want(Ship s) { return s.id == g_want; }
Ship s = fleet.Find(is_want);
```

## Target

```c
int want = 5;
Ship s = fleet.Find((Ship x) => x.id == want);
Ship t = fleet.FindOr((Ship x) => x.id == want, Ship(0, "", 0, 0));
```

## Implementation sketch (Strategy A, same as Where)

For `HOF_FIND` on List/Set (not Map):

```c
// Desugar conceptually:
T __cap_r = {};           // zero-init miss
for (int i = 0; i < recv.Count(); i++) {
    T __cap_e = recv.Get(i);
    if (/* pred with free vars */) {
        __cap_r = __cap_e;
        break;
    }
}
// stmtexpr result = __cap_r
```

Notes:

* Match existing Find miss semantics (zero-init / `Alive()` pattern).  
* Prefer Count/Get loop over for-in for by-value class elements (same as Where
  open-code comment for Hit/Ship).  
* `FindOr`: second arg is default value — open-code init `__cap_r = default`
  instead of zero.  
* Map has no Find today; skip.

## Capturing Select (optional same sprint)

```c
int thr = 10;
auto names = fleet.Select((Ship s) => s.heat > thr ? s.callsign : "");
```

Open-code: create result List, loop, `Add(proj_expr)`.  
Method-generic monomorph path still needed for non-capturing Select (P1).

## Validation

Extend `val-042-lambda-capture.cy`:

```c
int want = 3;
auto xs = List<int>();
// ... Add 1..5
int hit = xs.Find((int x) => x == want);
check(hit == 3, "capturing Find");
```

## Effort

M: Find is small once Where open-code is used as template; FindOr + Select
capturing are incremental.
