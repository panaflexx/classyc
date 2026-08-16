# P1 sketch — Select method-generic on stack / value List receivers

## Status

**Bug (reproduced 2026-07-15).**  
`List<U> Select<U>(…)` is a method type parameter. Monomorphization attaches
reliably when the receiver specialization is first seen as **`List<T>*`**, but
often fails with `class has no member Select` when the only uses are **stack
value** `List<T>` / `auto xs = List<T>()`.

## Repro matrix

| Pattern | Result |
|---------|--------|
| `List<int>* p = new List<int>{…}; p->Select<int>(fn)` | PASS (`val-031`) |
| `auto xs = List<int>(); xs.Select<int>(fn)` | **FAIL** — no member Select |
| Heap Select first, then stack Select same T | PASS (`probe-select-after-new.cy`) |
| `void Seed(List<T>*); Seed(&xs); xs.Select(…)` | PASS (`probe-select-seedfn.cy`) |
| Aurora/neon full programs | PASS (helpers take `List<Ship>*` / `List<Pilot*>*`) |
| Neon `List<LapSample>` only as value | Open-coded (no Select) |

Probes: `sketch/probe-select-*.cy`.

## Why this matters

House style is value-first:

```c
auto samples = List<LapSample>();
auto ms = samples.Select((LapSample s) => s.ms);  // should work
```

Today that is the one LINQ method that *looks* documented but fails without a
pointer-shaped specialization somewhere in the TU. Where/Take/Copy already work
on pure stack lists (non-generic methods).

## Likely root cause (inspection)

Method generics live on the class template and are instantiated at call sites
with explicit/inferred `U`. Specialization of `List<T>` for **value vs pointer
receivers** may:

1. Build two different “views” of the specialized class, or  
2. Only walk / clone method-generic members when the first complete type is
   `List<T>*` (common path for historical heap-only collections), or  
3. Fail to re-export method-generic slots when the tag is completed via
   stack ctor `List<T>()` alone.

Search centers (in `src/classyc.c`):

* method type-parameter parsing / monomorph (`Select<U>`)
* generic class specialization entry points for `N_NEW` vs stack ctor /
  `ClassName()` call
* call resolution when receiver is `TM_CLASS` (value) vs `TM_PTR` to class

## Fix plan

### Goal

```c
// cy-validate/val-046-select-stack.cy
auto xs = List<int>();
xs.Add(1); xs.Add(2);
auto d = xs.Select<int>(times2);
check(d.Count() == 2 && d.Get(0) == 2, "stack Select int");

auto samples = List<LapSample>();
samples.Add(LapSample(1, 100));
auto ms = samples.Select((LapSample s) => s.ms);
check(ms.Get(0) == 100, "stack Select value-T → int");
```

No requirement that a `List<T>*` appear earlier in the TU.

### Approach A — fix specialization (preferred)

When specializing `List<T>` (or any class with method type params), always
install method-generic templates on the specialized class **regardless of
whether the first use is value or pointer**.

Checklist:

1. Find where non-generic methods are cloned into `__generic_List_*`.  
2. Ensure method-generic decls (`Select<U>`) are cloned the same way for the
   first specialization trigger (stack ctor, `auto x = List<T>()`, brace-init,
   or `new`).  
3. Call-site monomorph of `Select<int>` must resolve `this` as either
   `List*` or value (already auto-deref for methods).  
4. Regression: val-031 heap paths stay green; add val-046 pure stack.

### Approach B — open-code Select like capturing Where (fallback)

If monomorph is too deep short-term: extend HOF open-code to `Select` /
`Select<U>` (build `List` of projected type). Loses thin-fn-ptr Select but
unblocks stack receivers. Prefer A for ABI consistency with val-031.

### Approach C — document “prime with List*” (reject)

Would preserve a footgun. Do not ship as the answer.

## Done when

- [ ] Pure stack `List<int>` / `List<Ship>` / `List<LapSample>` Select without
      any `List<T>*` in the TU  
- [ ] `val-046-select-stack.cy` green  
- [ ] neon-grid LapSample section uses real Select (delete open-code comment)  
- [ ] val-031 still green  

## Effort

Medium (2–3 days): mostly hunting specialization; fix may be small once found.
