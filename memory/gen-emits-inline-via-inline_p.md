---
name: gen-emits-inline-via-inline_p
description: How ClassyC gen decides MIR_INLINE vs MIR_CALL for a call site
metadata:
  type: reference
---

Gen emits `MIR_INLINE` (instead of `MIR_CALL`) at a direct call site iff the
callee's `((decl_t) func_expr->def_node->attr)->decl_spec.inline_p` is set
(src/classyc.c ~30916 / ~31102). MIR expands `MIR_INLINE` at gen/JIT time, so the
win does NOT show up in `-fdump-mir-stats` insn/call counts — it lands in codegen.

`midopt_run` step 4 (src/midopt.c ~2296) stamps `inline_p` on kept methods:
name whitelist (Count/IsEmpty/Capacity) PLUS shape-based
`midopt_trivial_scalar_getter_p` (single `return <arithmetic/enum>` body, no
nested calls, readonly). Do NOT inline pointer-into-`this` returns (Get/GetMut) —
MIR_INLINE of those miscompiles chaining. Related: [[optimizer-landscape]].
