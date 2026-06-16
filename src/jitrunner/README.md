# classyc jitrunner — hot-reload .bmir runner

A **dogfood** tool written in ClassyC that provides a fast iterative development
cycle: edit your source, and the runner automatically recompiles and re-executes
via MIR JIT.

## Architecture

```
┌──────────────┐      ┌────────────────┐      ┌───────────────┐
│  FileWatcher  │─────▶│  RunSession     │─────▶│  JIT bridge   │
│  (inotify)    │      │  (fork + exec)  │      │  (mir-bridge) │
└──────────────┘      └────────────────┘      └───────────────┘
                                                      │
                        ┌────────────────┐            │
                        │  DebugAdapter   │◀───────────┘
                        │  (future DAP)   │
                        └────────────────┘
```

### Components

| File | Language | Purpose |
|------|----------|---------|
| `jitrunner.c` | **ClassyC** | Main program — CLI, classes, file watcher, fork runner |
| `mir-bridge.c` | C (gcc) | Thin bridge wrapping MIR's DLIST macros into plain functions |
| `mir-bridge.h` | C header | API declarations consumed by the ClassyC code |
| `build.sh` | Bash | Build script: classyc → bmir → obj + gcc → link |

### Key design decisions

- **Fork isolation**: Each JIT run happens in a forked child process. A crash
  or infinite loop in the user's code cannot take down the runner.
- **MIR bridge**: ClassyC can't expand MIR's generic DLIST/VARR macros, so
  `mir-bridge.c` wraps them in plain C functions that ClassyC can `extern`.
- **DAP stub**: The `DebugAdapter` class has hooks for pre-run, post-run, and
  file-change events. Future work will implement the VS Code / Zed Debug
  Adapter Protocol over this skeleton.

## Building

From the project root:

```bash
bash src/jitrunner/build.sh
```

Prerequisites: `bin/classyc`, `bin/b2obj`, and `lib/libmir_static.a` must
already be built (the normal CMake build produces these).

## Usage

### Run a .bmir file

```bash
# First compile your source to .bmir
./bin/classyc -c -o /tmp/hello.bmir examples/classy-classes.c

# Then JIT-run it
./bin/jitrunner /tmp/hello.bmir
```

### Watch mode (hot-reload)

```bash
# Watch the .bmir file; re-runs whenever it changes
./bin/jitrunner /tmp/hello.bmir --watch
```

### Compile + run + watch

```bash
# The runner compiles the .c to .bmir, runs it, watches for source changes
./bin/jitrunner --compile examples/classy-classes.c --watch
```

### JIT modes

```bash
./bin/jitrunner program.bmir --mode lazy    # default — JIT on first call
./bin/jitrunner program.bmir --mode gen     # full JIT all functions upfront
./bin/jitrunner program.bmir --mode interp  # interpreter (slow, good for debugging)
```

## ClassyC features used

This tool exercises many ClassyC extensions as a dogfood test:

- **Classes** with constructors, destructors, and methods (`RunConfig`,
  `RunResult`, `DebugAdapter`)
- **`String` type** and string methods
- **f-strings** for formatted output
- **`defer delete`** for RAII-style cleanup
- **`new` / `delete`** heap allocation
- **`File.exists()`** static method from `include/file.h`

## DAP support (in progress)

The `DapServer` class in `dap.h` implements the DAP wire protocol over TCP:

- **`include/tcp.h`** — minimal TCP server/client header (adapted from nanoproxy)
- **`src/jitrunner/dap.h`** — DAP protocol: Content-Length framing, JSON messages via `dict`/`json()`

### Implemented DAP messages

| Direction | Message | Status |
|-----------|---------|--------|
| → | initialize | ✓ returns capabilities |
| → | launch | ✓ extracts program path |
| → | configurationDone | ✓ |
| → | threads | ✓ (single thread) |
| → | disconnect | ✓ |
| → | setBreakpoints | stub (accepts, no-op) |
| → | continue | stub |
| → | stackTrace | stub |
| ← | initialized | ✓ |
| ← | exited | ✓ |
| ← | terminated | ✓ |
| ← | output | ✓ (console + stderr) |

### Next steps

1. Wire `--dap <port>` flag into `jitrunner.c` main loop
2. Use MIR's debug info (`-g` flag) to map source locations
3. Implement real breakpoint support (ptrace or signal-based)
4. Expose stack frames and variables through DAP
5. Create a `launch.json` template for VS Code / Zed
