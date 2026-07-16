# ClassyC jitrunner — status, debugging, roadmap

**Dogfood tool:** JIT-run `.bmir` programs, optionally hot-reload, and speak the
Debug Adapter Protocol (DAP) so editors (Zed, VS Code) can break / step / continue.

Primary sources:

| File | Role |
|------|------|
| `jitrunner.cy` | ClassyC main — CLI, fork runner, DAP entry (`--dap`, `--dap-stdio`) |
| `dap.h` | DAP wire protocol (Content-Length + JSON) over TCP or stdio |
| `mir-bridge.c` / `.h` | Plain-C MIR wrappers + cooperative interp debugger |
| `build.sh` | Build: classyc → bmir → obj + gcc link with `libmir_static.a` |

Related tests (repo root): `test_dap_protocol.py`, `test_dap_debug.py`.

---

## What works today

### Run & rebuild

```bash
# From project root (needs bin/classyc, bin/b2obj, lib/libmir_static.a)
bash src/jitrunner/build.sh

# Plain JIT run
./bin/classyc -I include -c -g -o /tmp/prog.bmir examples/classy.cy
./bin/jitrunner /tmp/prog.bmir

# Modes: lazy (default) | gen | interp
./bin/jitrunner /tmp/prog.bmir --mode lazy
./bin/jitrunner /tmp/prog.bmir --mode gen
./bin/jitrunner /tmp/prog.bmir --mode interp

# Watch .bmir or source recompile+run
./bin/jitrunner /tmp/prog.bmir --watch
./bin/jitrunner --compile examples/classy.cy --watch
```

- Fork isolation: user code crashes stay in the child.
- Output capture in DAP mode (stdout/stderr → DAP `output` events).
- Independent, strictly increasing DAP server `seq`; responses carry correct `request_seq`.

### DAP (stdio + TCP)

```bash
# Editor-friendly (Zed launches this)
./bin/jitrunner --dap-stdio path/to/program.bmir --mode interp

# Optional TCP server
./bin/jitrunner --dap 4711
```

Handshake (correct DAP ordering — do **not** run early):

1. `initialize` → response + `initialized` event  
2. `launch` (stores program; **does not start**)  
3. `setBreakpoints` (may appear before/after launch)  
4. `configurationDone` → **then** start the program  
5. `exited` / `terminated` on finish; `disconnect` ends the session  

Logging: `dapdebug.log` by default (override with `CLASSYC_DAP_LOG=path` or empty to disable).

### Cooperative JIT debugging (interp mode)

When breakpoints are set, jitrunner forces **MIR interpreter** mode and installs
a line hook. Architecture:

```
Editor (Zed)
    │ DAP stdio
    ▼
jitrunner parent
    │ fork + env CLASSYC_DEBUG_BPS / CLASSYC_DEBUG_CTRL_FD
    │ ctrl pipe: parent writes 'c'|'s'|'n' → child reads
    ▼
child: load .bmir → jit_interp_dbg_* → MIR_link(interp)
    │ hit BP / step → stderr "__DAP_BRK__" / "__DAP_STEP__"
    │ parent → DAP stopped → wait client → resume byte → continue
```

| Capability | Status |
|------------|--------|
| Breakpoints (line) | ✓ — mapped to nearest executable MIR source loc |
| `continue` | ✓ |
| `stepIn` | ✓ — every new source line (including into methods) |
| `next` / step-over | ✓ — call-depth aware; skips into callees |
| `stepOut` | △ — treated as continue (no full “return frame” yet) |
| `stackTrace` | ✓ — last stop file:line:col (single fake frame) |
| `threads` | ✓ — single thread `main` |
| `scopes` / `variables` | △ — empty Locals stub (no real values yet) |
| Multi-file / path match | ✓ — absolute path or basename suffix match |

**Hard requirement: compile with `-g`.**

```bash
./bin/classyc -I include -c -g -o examples/foo.cy.bmir examples/foo.cy
```

Without `-g`, the bmir has no source-line table (`sourced_insns=0`) and breakpoints never fire.

### Design choices that matter

1. **No MIR inlining in interp/debug** (`MIR_set_no_inlines(1)` during `MIR_link`).  
   MIR’s `process_inlines` would bake callees into the caller so call-depth step-over is impossible.

2. **Nearest-line BP resolution** (`jit_interp_dbg_add_bp_resolved`).  
   Blank lines / headers / pure decls often have **no** MIR location. Requested line *L* is remapped to the smallest executable line ≥ *L* in the same file (else closest). Example from `classy-aurora-ops.cy`: 210→212, 275→277, 224→227.

3. **Resume via plain C** (`dap_ctrl_write_byte` in mir-bridge) so we never clash with ClassyC `File::write`.

4. **DAP message command strings** are copied before free — avoid UAF when handling `continue` / step after `scopes`/etc.

---

## What does *not* work / limitations

| Area | Reality |
|------|---------|
| **Variables / watch / evaluate** | Not implemented. `scopes` returns an empty Locals shell; `variables` is `[]`. |
| **Real stack frames** | One synthetic frame (`main` at stop site). No walk of MIR call stack / registers. |
| **stepOut** | No dedicated “until return”; currently resume-as-continue. |
| **Lazy / gen mode debugging** | Line BPs only work in **interp**. Generated machine code is not patched (no INT3 path wired into DAP). |
| **Conditional / logpoint BPs** | Not supported. |
| **Exception breakpoints** | Not supported. |
| **Multi-thread** | Single logical thread only. |
| **Hot-reload + DAP** | Watch mode and DAP are separate paths; no combined “reload debuggee” session. |
| **Inexact source ↔ MIR** | Even with `-g`, some statements share lines or vanish after optim/simplify; remapping helps but isn’t DWARF-perfect. |
| **Library headers in line maps** | `.bmir` can include locs from `include/list.h`, system headers, etc. Filenames for user BPs still match on the user source file. |
| **TCP DAP vs stdio** | Both paths share helpers; stdio is what Zed uses. TCP is less battle-tested. |

---

## Editor setup (Zed)

Typical launch shape (adapt paths):

```json
{
  "adapter": "custom",
  "label": "ClassyC JIT",
  "request": "launch",
  "program": "${workspaceFolder}/examples/classy.cy.bmir",
  "cwd": "${workspaceFolder}",
  "args": [],
  "tcp": false,
  "command": "${workspaceFolder}/bin/jitrunner",
  "command_args": ["--dap-stdio", "${workspaceFolder}/examples/classy.cy.bmir", "--mode", "interp"]
}
```

Checklist when “breakpoints do nothing”:

1. Binary rebuilt: `bash src/jitrunner/build.sh` after MIR/jitrunner changes.  
2. Program built **with `-g`**.  
3. `program` points at that `.bmir`.  
4. `dapdebug.log` shows `setBreakpoints` **before** run (`configurationDone` then load BPs).  
5. Log shows `CHILD add bp …` and `BP resolve …` (if line was remapped).  
6. Log shows `CHILD BREAK` / `stopped` — if only `exited`, line has no map or path mismatch.

---

## How debugging works (internals)

### Parent

- After handshake: if `/tmp/classyc-dap-bps.txt` non-empty → create ctrl pipe, set env, force `jit_mode = interp`.
- `run_bmir(..., out_cb)`: pipe child stdout/stderr; on `__DAP_BRK__` / `__DAP_STEP__` emit DAP `stopped`, then **block reading DAP** until `continue` / `next` / `stepIn` / disconnect.
- Resume: write one byte on ctrl pipe (`c` / `s` / `n`).

### Child

- Load bmir, install `JIT_interp_dbg_state`, resolve each BP line against module source maps.
- `interp_line_hook` (MIR): on each sourced insn, decide break vs step; suppress re-hits on the **same** line until the line changes.
- On break: print marker to stderr, **block** reading ctrl pipe, then apply step/continue state on `g_active`.

### Step modes (`mir-bridge.c`)

| Mode | `step_mode` | Behavior |
|------|-------------|----------|
| continue | 0 | Only user breakpoints |
| step-in | 1 | Stop on every new source line |
| step-over | 2 | Stop when `call_depth <= next_depth` (depth snapped at step request) |

Call depth is maintained by a MIR interp hook around `MIR_CALL` / `IC_IMM_CALL` (disabled inlining is mandatory for this).

---

## Tests

From repo root:

```bash
# Seq protocol + launch without BPs
python3 test_dap_protocol.py

# Breakpoint → threads/stackTrace/scopes → stepIn → continue
python3 test_dap_debug.py
```

Smoke with aurora (needs `-g` bmir):

```bash
./bin/classyc -I include -c -g -o examples/classy-aurora-ops.cy.bmir examples/classy-aurora-ops.cy
# then DAP-debug that path from the editor
```

Env flags useful while hacking:

| Env | Effect |
|-----|--------|
| `CLASSYC_DAP_LOG` | Wire log path (default `dapdebug.log` in stdio mode) |
| `CLASSYC_DEBUG_STEP` | Verbose step/call_depth on child stderr |
| `CLASSYC_DEBUG_ICODE` | Dump MIR insn list as interpreter generates icode |

---

## Roadmap (prioritized)

### P0 — Product polish for editor use

- [ ] **Variables**: map MIR `dbinfo` vars / registers into DAP `variables` for the stopped frame.
- [ ] **Honest setBreakpoints response**: return *resolved* lines (and `verified: false` when no map), not only the requested numbers.
- [ ] **stepOut**: run until `call_depth` drops below start depth, then stop.
- [ ] Document/package a **Zed/VS Code launch template** checked into the repo.

### P1 — Correctness & scale

- [ ] Multi-frame `stackTrace` from MIR interp nesting / call stack.
- [ ] Catch / report uncaught ClassyC exceptions as DAP `stopped` (exception reason).
- [ ] Avoid leftover global `/tmp/classyc-dap-bps.txt` races (per-session temp file).
- [ ] Clearer stderr hygiene in child (less “CHILD ENV …” noise unless verbose).

### P2 — Performance path

- [ ] **Machine-code debug path** (`--mode gen`/`lazy`): use MIR line maps + INT3 / self-debug helpers already sketched in `mir-bridge` (`jit_self_set_breakpoint`, linemap query) — optional, much harder than interp.
- [ ] Fast continue without full interpreter when no BPs.

### P3 — Nice-to-have

- Conditional breakpoints, hit counts, logpoints.  
- DAP `pause` / `restart`.  
- Source-map path rewrite (workspace-relative ↔ absolute).  
- Combined watch + DAP for hot-reload debugging.

---

## Exit criteria for “first-class editor debug”

- [x] DAP stdio seq protocol stable  
- [x] Wait for `configurationDone` before run  
- [x] Line breakpoints with `-g` + nearest-line resolve  
- [x] Continue / step-in / step-over  
- [x] Single-frame stackTrace + empty scopes stub  
- [ ] Local variables visible in the editor  
- [ ] stepOut  
- [ ] Multi-frame stack  
- [ ] Verified line remap reported back to the UI  

---

## Historical notes (pitfalls we hit)

1. **Early launch** — starting on `launch` before `setBreakpoints` / `configurationDone` made BPs appear “ignored”.  
2. **Missing `-g`** — silent no-op breakpoints.  
3. **Exact line only** — aurora BPs on blank/header lines never hit until nearest-line resolve.  
4. **MIR inlining** — step-over entered “methods” because there was no CALL after `process_inlines`; fixed by `MIR_set_no_inlines` in interp.  
5. **UAF after scopes** — free of DAP buffer before strcmp on `continue` killed the adapter; command names are now copied.  
6. **`write` vs `File::write`** — resume must go through C helper, not ClassyC symbol.

---

*Last updated after DAP stdio hardening, nearest-line BPs, and call-depth step-over (interp mode).*
