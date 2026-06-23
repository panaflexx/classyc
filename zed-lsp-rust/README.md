# ClassyC — Zed extension

A tiny Zed language extension that registers the **ClassyC** language for `.cy`
files and launches the **`classyc-lsp`** server, so the editor understands
ClassyC's extensions (`class`, `String`, `dict`, `new`, `defer`, lambdas,
generics, …) instead of having clangd flag them as errors.

The extension itself does almost nothing: it maps `.cy` → ClassyC (reusing the
Tree-sitter **C** grammar for highlighting) and tells Zed how to start the
server. All diagnostics come from `classyc-lsp`, which drives the real compiler
front end (preprocess → parse → check, no codegen).

## Prerequisites

1. Build the language server (from the repo root):

   ```sh
   cmake .
   make classyc-lsp        # produces ./bin/classyc-lsp
   ```

2. A Rust toolchain with the WebAssembly target (Zed compiles the extension):

   ```sh
   rustup target add wasm32-wasip1
   ```

## Install as a dev extension

1. In Zed, open the command palette and run **`zed: install dev extension`**.
2. Select this directory (`zed-lsp-rust`). Zed builds it and clones the C grammar.
3. Open a `.cy` file. The status bar should show **ClassyC**, and diagnostics
   appear as you type/save.

## How the server is located

`language_server_command` (in `src/lib.rs`) resolves `classyc-lsp` in this order:

1. An explicit `settings.json` override:

   ```jsonc
   "lsp": {
     "classyc-lsp": { "binary": { "path": "/abs/path/to/bin/classyc-lsp" } }
   }
   ```

2. `classyc-lsp` on your `PATH`.
3. `<worktree-root>/bin/classyc-lsp` — i.e. the binary this repo builds.

So when you open the ClassyC repo itself, no configuration is needed.

## Files

| File | Purpose |
|------|---------|
| `extension.toml` | extension metadata, C grammar pin, language-server registration |
| `Cargo.toml`, `src/lib.rs` | the minimal Rust glue that starts `classyc-lsp` |
| `languages/classyc/config.toml` | language name, `.cy` suffix, comments, brackets |
| `languages/classyc/highlights.scm` | Tree-sitter highlight queries (C-based) |

## Notes / limitations

- Highlighting is C-based; ClassyC-only keywords render as identifiers. Semantic
  correctness (diagnostics) is what `classyc-lsp` provides.
- The server currently provides **diagnostics** only. Hover / go-to-def /
  completion are natural follow-ons (the AST and symbol tables are already built
  during analysis).
