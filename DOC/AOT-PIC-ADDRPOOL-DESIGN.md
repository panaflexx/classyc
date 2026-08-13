# Design sketch: PIC AOT via `.mir.addrpool` (ClassyC b2obj path)

Date: 2026-08-13  
Status: **implemented** (x86-64 Linux AOT; 2026-08-13)  
Goal: allow Linux AOT objects to link under default **PIE** toolchains without
`DT_TEXTREL`, while **keeping** ClassyC’s preferred post-hoc object writer
(`src/b2obj.c` / `src/b2objmac.c`) rather than adopting MadC’s full
`MIR_object_*` stack.

### Implementation summary

- When `MIR_gen_set_save_relocs` is on, x86-64 gen keeps **no ABS64 in `.text`**:
  item addresses and call/jump targets load from a module-level
  **`.mir.addrpool`** via `R_X86_64_PC32`; switch tables move ABS64 slots into
  the pool as well.
- `MIR_gen_get_addrpool` exposes pool bytes + ABS64 pool relocs to `b2obj`.
- `b2obj` emits `.mir.addrpool` + `.rela.mir.addrpool`; `classyc-aot.sh` no
  longer passes `-no-pie` on Linux.
- aarch64 Linux already uses adrp/GOT-style refs for object mode; switch-table
  pool move there is optional follow-up (Phase C in the sketch below).

## Problem

Today’s Linux AOT path (eager `MIR_gen` + `MIR_gen_set_save_relocs` → copy
`machine_code` into `b2obj`) is **non-PIC**:

- x86-64 object codegen materializes item/func addresses as **absolute
  64-bit** slots (const pool at end of each function, `R_X86_64_64`).
- Linked executables therefore need **`-no-pie`** (see `classyc-aot.sh`).
- Default modern distro link lines (PIE) would produce text relocations /
  loader friction.

macOS `b2objmac` is already closer to PIE (RIP-relative LEA / GOTPCREL on
x86_64; PAGE/GOT patterns on arm64). Linux is the gap.

## What MadC got right (steal the idea, not the whole stack)

MadC’s **object mode** rewrites references during codegen so that:

1. Absolute item addresses and switch tables live in a **shared section**
   `.mir.addrpool` (GOT-shaped).
2. **Text** only contains PC-relative or page-relative loads of those slots
   (x86-64: `PC32` into pool; aarch64: `adrp`+`ldr` / `adrp`+`add`).
3. ABS64 relocs exist only on pool/data — never as dynamic relocs in `.text`
   → no `DT_TEXTREL`.

That requires **capture-before-publish** (or equivalent rewrite) in gen, not
only a smarter ELF writer. Post-hoc rewriting of already-absolute code is
fragile; MadC abandoned that approach for a reason.

## Proposed ClassyC shape (phased)

### Phase A — Gen “object PIC mode” (x86-64 first)

Add a ClassyC-controlled flag (name TBD), e.g. `MIR_gen_set_aot_pic(ctx, 1)`,
orthogonal to MadC’s full object builder:

| Concern | Behavior when AOT-PIC on |
|--------|---------------------------|
| Item REF in code | Emit load from pool slot, not imm64 absolute |
| Switch tables | Pool of ABS64 labels; text holds PC-rel pointer to table |
| Const pool | Prefer **module-level** `.mir.addrpool` over per-func tail pools |
| Reloc records | `func->relocs` gains pool-relative PC32 (text) + ABS64 (pool) |
| Publish | Still fill `machine_code` for b2obj copy (optional: also keep
  pool bytes separately on module or gen ctx) |

**Deliverable:** same b2obj pipeline, but `.text` has no ABS64; new section
`.mir.addrpool` (or `.got`-like PROGBITS) with ABS64 to symbols.

### Phase B — b2obj packaging

| Change | Detail |
|--------|--------|
| New section | `.mir.addrpool` (SHF_ALLOC\|WRITE or read-only PROGBITS + RELA) |
| Relocs | Emit `.rela.mir.addrpool` (ABS64) and PC32 text→pool |
| Symtab | Pool is not a symbol; section symbol for pool base |
| Link line | Drop `-no-pie` in `classyc-aot.sh` once green |
| TLS | Keep existing `.tdata` / TPOFF32 (orthogonal) |

No ET_EXEC emitter, no in-process loader, no multi-object merge — still
**ET_REL + system `ld`**.

### Phase C — aarch64 Linux ELF

Port the same pool model with ELF `R_AARCH64_*` (not Mach-O markers).
`b2objmac` already has PIE-oriented patterns; share naming where possible.

## Non-goals (explicit)

- Replacing b2obj with `MIR_object_emit_executable` / RELRO / `DT_DEBUG`
  (final link already provides these).
- In-process ET_REL loader / multi-`.o` merge (`ld` / `ld -r` suffice).
- Replacing `mir-dbinfo` + ClassyC DWARF with MadC `MIR_debug_*`.

## Correctness checklist

1. No ABS64 (or dynamic ABS64) in `.text` under AOT-PIC.
2. Self-compiling / large modules (e.g. oggenc-scale) link with PIE gcc.
3. TLS LE still works with `-fPIC`/`-pie` (TPOFF relative to TP, not PC).
4. DWARF low_pc / `.debug_frame` still relocate against `.text` section symbol.
5. cyreg sections unchanged.

## Effort estimate

| Slice | Effort | Risk |
|-------|--------|------|
| x86-64 gen AOT-PIC + b2obj pool section | medium–large | medium (gen pattern matching) |
| aarch64 Linux ELF mapping | medium | medium |
| Drop `-no-pie` + CI | small | low once (1) green |

## Suggested order relative to recent work

1. **Done:** `.debug_frame` (b2obj + b2objmac) + full DWARF on b2objmac.  
2. **Next product jump:** Phase A/B PIC as above.  
3. Optional: mine weak/linkonce only if ClassyC gains real weak attributes.

## References

- MadC: `ext/mir-madc/mir-gen-x86_64.c` `target_object_capture`,  
  `ext/mir-madc/mir-gen-aarch64.c` same,  
  `ext/mir-madc/mir-debug.c` `.mir.addrpool` emission.
- ClassyC today: `src/b2obj.c` (non-PIC ET_REL), `classyc-aot.sh` (`-no-pie`),  
  `MIR_gen_set_save_relocs` + `func->relocs`.
'''