---
name: optimizer-landscape
description: Non-obvious constraints for ClassyC midopt/gen optimization work
metadata:
  type: project
---

Findings from the 2026-07-21 midopt optimization pass (Tasks 1–3):

- **Scalar locals are already register-promoted.** `process_func_decls_for_allocation`
  (src/classyc.c ~17300) sets `decl->reg_p = TRUE` for every `scalar_type_p` local
  EXCEPT in functions containing a `try` (with exceptions on), where longjmp reverts
  callee-saved registers to setjmp-time values. So a from-scratch mem2reg pass is
  redundant. The one real remaining pessimization: try-functions memory-home ALL
  scalars; a precise fix (only memory-home scalars ASSIGNED inside a try body) is
  safe in principle but has an allocation/longjmp-dependent miscompile surface the
  cy-validate suite cannot reliably gate — deferred by design.
- **MIR mem-GVN is `-O3`-gated** but correct (cy-validate 54/0/0 at -O3); benefit on
  ClassyC hot paths is noise because they're already open-coded (R1/R2) + scalars in regs.
- **Two collection-method whitelists, do not confuse them:** `midopt_safe_methods`
  treats growth (Add/Insert/Set) as OOB-SAFE (length only grows) — used by
  `midopt_iv_hazard_p` for guard elision. `midopt_pure_coll_methods` is the STRICT
  read-only set (no growth) — required whenever a value must be loop-INVARIANT
  (R-LICM Count hoist, R2 for-in by-ref). See [[gen-emits-inline-via-inline_p]].
- **R-LICM Count hoist** (gen memoizes a proven-invariant `recv.Count()` bound across
  the N_FOR two-emission cond so it runs once): relies on `top_gen` never resetting
  `reg_free_mark`, so a pre-header temp reg is uniquely named and dominates the reuse.
