# Bug 001 — root cause analysis: `while (A && !(B && C))` miscompiled at -O2

## TL;DR

The bug is in **ext/mir/mir-gen.c**, in the **"SSA combine" pass** (`ssa_combine`,
address-operand folding). The guard `cycle_phi_p()` that is supposed to prevent
folding an address computation *through a loop phi* only detects phis of
**single-block loops** (self-loops). For a multi-block loop body it returns
FALSE, so the combiner folds `base + (phi_reg + const)` into a memory operand
`mem[base, phi_reg]{disp=const}` in a *later* block of the loop — after the
conventional-SSA copy on the loop back edge has already **overwritten
`phi_reg` with the next-iteration value**. The load then reads `s[j+3]`
instead of `s[j+2]`, so the `'\n'` test never fires and the loop runs to the
end of the buffer (`got=5`).

This is generic MIR JIT behavior, not a classyc front-end issue: stock
`c2m` built from the same ext/mir fails identically, and the exact same code
exists in **upstream vnmakarov/mir master** (verified by inspection of
upstream `mir-gen.c`: `cycle_phi_p`, `var_plus_const`, `var_mult_const`,
`var_plus_var`, `update_addr_p`, `ssa_combine` are byte-identical).

## Reproduction / pass isolation

- Correct with `-ei`, `-O0`, `-O1`; wrong at `-O2` (default). SSA combine only
  runs at `-O2+`, and the trigger additionally needs GVN (also `-O2+`) to
  create the re-associated `j+2` expression (see below).
- `./bin/c2m -O2 -dg2 /tmp/b001.c -eg 2>gen.log` shows per-pass MIR for
  `scan_cond`.

## Detailed mechanism

`scan_cond` compiles (rotated loop) to a 3-block loop:

- **BB5** (`L0`, loop header): `phi j`, `j' = j+1`, `I_6 = ?`, `bge L1, j+1+1, n`
- **BB6**: load `s[j']`, `bnes L0, ., 13`  (continue if not `'\r'`)
- **BB7**: load `s[j'+1]`, `bnes L0, ., 10` (continue if not `'\n'`, else exit)

**Step 1 — GVN** re-associates `I_9 = j' + 1` (address of `s[j'+1]`, used in
BB7) into `I_6 = j + 2` where `j` is the *phi result* in BB5, and reuses it
for the loop-bound test. After GVN/DCE, BB7's load address is
`t8 = U0_s + I_6`, with `I_6 = I0_j@1 + 2` defined in BB5 reading the phi
register directly. (This is why -O2 is required and why a single-load
condition like `while (j+1<n && s[j+1]!='\n')` does *not* trigger: GVN must
create an expression on the phi register that is consumed in a block past
the latch branch.)

**Step 2 — conventional SSA** (`make_conventional_ssa`) renames the phi
webs: the phi in BB5 becomes `phi I0_j@1%0, ...` and copy insns
`mov I0_j@1%0, I_5@1` are inserted at the **tails of the loop-latch blocks
BB6 (insn 53) and BB7 (insn 54)** — before their conditional branches back
to L0. From this point on, `I0_j@1%0` is **not single-assignment**: on the
fall-through path BB6→BB7 it already holds the *next* iteration's `j`.

**Step 3 — ssa_combine** processes BB7's load and folds the address chain
through `update_addr_p` → `var_plus_const`:

```
Processing bb7
  combining insn  mov t9, i8:(t8)
    changing mem op 1 to  mov t9, i8:2(U0_s, I0_j@1%0)     <-- WRONG
```

MIR after ssa combine (from `-dg2`, function `scan_cond`):

```
BB   6
  25    mov  t3@1, i8:1(U0_s, I0_j@1%0)   ; s[j+1] — OK (copy 53 is *after* insn 25)
  53    mov  I0_j@1%0, I_5@1              ; conventional-SSA copy: j := j+1  (!!)
  27    bnes L0, t3@1, t4@1               ; conditional: falls through to BB7
BB   7
  31    mov  t9, i8:2(U0_S, I0_j@1%0)     ; intended s[j+2]; actually s[(j+1)+2] (!!)
  32    mov  t7@1, 10
  33    bnes L0, t9, t7@1
```

On the path BB5→BB6→BB7, insn 53 executes before insn 31, so the folded
operand `i8:2(U0_s, I0_j@1%0)` reads `s[j_old+3]` instead of `s[j_old+2]`
(= `s[j'+1]`). With `"AB\r\nCD"`: when `s[j'] == '\r'` (j'=2), BB7 checks
`s[4] == 'C'` instead of `s[3] == '\n'`, misses the CRLF, and the loop only
exits via `j+1 < n` with `j == 5`.

## The broken guard

`ext/mir/mir-gen.c`, ~line 5727:

```c
static int cycle_phi_p (bb_insn_t bb_insn) { /* we are not in pure SSA at this stage */
  ssa_edge_t se;
  if (bb_insn->insn->code != MIR_PHI) return FALSE;
  for (size_t i = 1; i < bb_insn->insn->nops; i++)
    if ((se = bb_insn->insn->ops[i].data) != NULL && se->def->bb == bb_insn->bb) return TRUE;
  return FALSE;
}
```

It is used in `var_plus_const` (line ~5756), `var_mult_const` (~5785) and
`var_plus_var` (~5799/5801) as:

```c
if ((se = res_ref->data) != NULL && se->def->bb != from_bb && cycle_phi_p (se->def)) return FALSE;
```

Intent: refuse to trace an address chain through a *loop* phi when the mem
op is in a different block, because in conventional SSA the phi register is
redefined on the back edge. But after `make_conventional_ssa`, a phi's
operands are defined by the copies at the **tails of the predecessor
blocks**, so `se->def->bb == bb_insn->bb` holds **only for self-loops**
(latch == header). For any loop whose body has more than one block (BB6/BB7
here), `cycle_phi_p` returns FALSE and the unsound substitution proceeds.

Uses *inside the phi's own block* are safe because conventional-SSA copies
are inserted after all original insns (just before the branch), and the
BB6-style case is safe only by the accident that the redefining copy 53
sits after the load — the dangerous case is precisely a use in a block
reached *through* a latch block, which the guard was meant to catch.

## Trigger characterization

Required shape (all confirmed experimentally):

1. A loop whose body spans **≥ 2 basic blocks past the header**, i.e. a
   short-circuit condition with two memory tests:
   `while (A && !(B && C))`, or equivalently `while (A && (B' || C'))`
   — the `!` is irrelevant; `while (j+1<n && (s[j]!=13 || s[j+1]!=10))`
   fails identically.
2. The **second** test loads at an address that GVN re-associates to
   `phi_reg + const` (here `s[j+1]` with `j+1` = `phi + 2`, reusing the
   loop-bound expression).
3. Element type is irrelevant: `int` arrays fail the same way (not a char
   signedness issue). A single-load condition (`while (j+1<n && s[j+1]!=10)`)
   does **not** trigger. The explicit-`break` form compiles correctly only
   because its CFG/GVN shape doesn't produce a cross-latch use of the phi
   register.

## Proposed fix (validated in /tmp, not applied to the submodule)

Minimal and safe: in `ext/mir/mir-gen.c`, refuse the cross-BB substitution
whenever the traced operand is defined by **any phi**, i.e. replace the four
occurrences of

```c
... && se->def->bb != from_bb && cycle_phi_p (se->def)) return FALSE;
```

with

```c
... && se->def->bb != from_bb && se->def->insn->code == MIR_PHI) return FALSE;
```

at lines ~5756 (`var_plus_const`), ~5785 (`var_mult_const`), and
~5799/5801 (`var_plus_var`). (`cycle_phi_p` then becomes unused and can be
deleted.)

Rationale: in this conventional-SSA form *every* phi destination register is
multiply-defined (by the `%0` copies in all predecessors), and any path from
the phi to a use in another block that passes through a predecessor-of-the-
phi-block tail crosses a redefinition. Only-same-block uses are provably
safe (copies are inserted after all original insns). The check is slightly
conservative for non-loop join phis dominating the use, costing at most one
folded add per address (the combiner still forms `mem[base,index]`; it just
stops folding at the first phi boundary — confirmed: BB7's operand becomes
`i8:(U0_s, I_6)` after the fix).

Validation performed (all in /tmp, gcc -O0 -g build of c2m from the fork
sources with only the 4-line patch):

- `bugs/001` reproducer: base `got=5 FAIL` → patched `got=2 PASS`
  (also with int arrays and the `||` variant).
- 215 tests in `ext/mir/c-tests/lacc/*.c` run under both base and patched
  c2m with `-eg`: outputs identical everywhere (the single differing test,
  `macro.c`, prints uninitialized stack bytes in both and returns the same
  exit code — pre-existing test flakiness, not a regression).
- `ext/mir/c-benchmarks` (sieve, binary-trees, funnkuch-reduce, nbody, hash,
  array, method-call, strcat, except): base vs patched outputs identical.

## Upstream relevance

The identical `cycle_phi_p`/`var_plus_const`/`var_mult_const`/`var_plus_var`/
`ssa_combine` code is present in upstream `vnmakarov/mir` master
(mir-gen.c), so upstream is presumably affected by the same miscompilation
and this should be reported/fixed there as well.
