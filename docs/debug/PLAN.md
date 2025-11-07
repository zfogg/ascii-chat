# Full Instrumentation Pipeline Plan

## Goals

- [x] Build a Clang libTooling executable that rewrites ascii-chat C sources, inserting per-statement logging calls.
- [x] Provide a runtime logging helper that records PID/TID, file, line, macro status, and source snippet via single write() calls with env-configurable filters.
- [x] Wire the tool into CMake so we can compile both original and instrumented builds easily while guaranteeing the original sources remain untouched.

## Steps

### Runtime Logging Library (lib/debug/)
- [x] Create `lib/debug/instrument_log.h` and `lib/debug/instrument_log.c` implementing `ascii_instr_log_line()` using async-signal-safe `write()`; include PID/TID, timestamp, macro flag, snippet.
- [x] Add env-configurable filters (e.g., `ASCII_INSTR_INCLUDE`, `ASCII_INSTR_THREAD`, `ASCII_INSTR_OUTPUT_DIR`) and per-thread file support.
- [x] Expose initialization/teardown helpers for thread-local log files.
- [x] Register the new source files in the existing library target definitions (via `cmake/debug/Instrumentation.cmake`).

### Instrumentation Tool (`src/debug/`)
- [x] Implement `src/debug/ascii_instr_tool.cpp` using Clang libTooling + AST visitors to insert `ascii_instr_log_line()` before each executable `Stmt`, capturing original source text via `SourceManager`.
- [x] Enforce “write-new-only” safety: validate target output directories, refuse to overwrite existing files, and abort on any attempt to reuse an existing path.
- [x] Handle macros by tagging expansion sites; log metadata distinguishing expansion use.
- [x] Add command-line options for selecting files/functions and for skipping functions marked with `ASCII_INSTR_SIGNAL_HANDLER` (via annotate attribute) macro.
- [x] Emit transformed sources to a dedicated output directory mirroring the input tree.

### Build System Integration (`cmake/debug/`)
- [x] Provide `cmake/debug/Targets.cmake` that defines the libTooling executable and `ascii-debug-runtime`, ensuring LLVM/Clang discovery happens there.
- [x] Expose `ascii_instrumentation_prepare()` / `ascii_instrumentation_finalize()` helpers in `cmake/debug/Instrumentation.cmake`, invoked from the root `CMakeLists.txt`.
- [x] Add cache option `ASCII_BUILD_WITH_INSTRUMENTATION` and integrate `cmake/debug/run_instrumentation.sh` to generate instrumented sources safely into `build/instrumented/`.

### Developer Documentation & Usage
- [ ] Document workflow in `docs/debug-instrumentation.md`: configuration, environment filters, per-thread logs.
- [ ] Highlight safety guarantees (no overwrites/deletions of source/.git) and recommended manual checks before use.
- [ ] Note limitations (performance overhead, sanitizers, signal-handler opt-outs) and post-processing tips.

## Optional Enhancements (time permitting)
- [ ] Provide a small post-processing helper in `src/debug/` that summarizes the last log per thread.
- [ ] Add unit tests (Criterion) for the logging runtime to ensure env filters behave correctly (skipped in macOS CI if needed).

---

## Part Two – Expanded Ideas from NOTES.md

1. **Macro-Aware Logging Enhancements**
   - Instrument both macro invocation sites and the expanded statements, tagging each log with `macro_invocation` / `macro_expansion` markers.
   - Add CLI toggles (`--log-macro-invocations`, `--log-macro-expansions`) to control noise levels.
   - Research `SourceManager::getImmediateMacroCallerLoc()` to improve correlation between invocation and expansion logs.

2. **Advanced Filtering & Noise Control**
   - Extend runtime filters to support regular-expression includes/excludes for files and functions.
   - Add rate limiting (e.g., `ASCII_INSTR_RATE=10` to log every Nth statement per thread).
   - Support `ASCII_INSTR_ONLY=server:*` to scope logging to module prefixes or glob patterns.

3. **Signal-Safe Postmortem Tooling**
   - Create a small analyzer that parses per-thread log files and reports the last executed statement per TID, highlighting divergence between threads.
   - Hook into crash handlers (where safe) to emit the last statement snapshot automatically.

4. **SanitizerCoverage Mode (Alternate Path)**
   - Prototype the lightweight `__sanitizer_cov_trace_pc_guard` variant to cross-check crash locations without source rewriting.
   - Build a symbolizer helper that resolves PCs to `file:line` and optionally prints the on-disk source snippet for parity with the source-to-source path.

5. **Signal Handler Opt-In/Out Support**
   - Implement optional annotation macros (e.g., `#define ASCII_INSTR_SIGNAL_HANDLER`) that map to Clang attributes for exclusion.
   - Document guidelines for marking async-signal-safe sections to avoid interference from inserted logging.

6. **Documentation & Examples**
   - Add `docs/debug-instrumentation.md` with configuration walk-throughs, env var reference, and example workflows (e.g., "instrument only client display code").
   - Provide scripts/snippets for tailing logs, filtering by thread, and mapping macros to expansions.

7. **Future Ideas (Backlog)**
   - Explore dynamic instrumentation with `rr`/`dynamorio` for environments where recompilation is not possible.
   - Investigate coupling with AddressSanitizer/UBSan to capture faulting addresses and correlate them with instrumented logs.

---

## Part Three – Longer-Term Exploration

1. **Selective Instrumentation Dialing**
   - Build an interactive configuration (toml/yaml) to declare instrumentation focus areas per build (e.g., `instrument: ["src/client/", "lib/network/"]`).
   - Investigate incremental instrumentation builds that reuse previous transformed source trees when only subsets of inputs change.

2. **Inline Context Augmentation**
   - Enrich log lines with surrounding context (preceding/following statements) and control-flow markers (enter/exit blocks).
   - Optionally inject dynamic variable snapshots (opt-in, careful to avoid side effects) for debugging complex state transitions.

3. **Visualization Tooling**
   - Prototype a TUI or web UI that streams instrumented logs and visually correlates them with source code (e.g., highlight the last executed lines per thread).
   - Integrate with existing log viewers or IDE plugins to jump to the last executed locations automatically.

4. **Symbolization & Coverage Fusion**
   - Combine instrumented logs with sanitizer coverage data to provide branch-level crash breadcrumbs.
   - Use DWARF debug info to map instrumented statements to richer context (class/function metadata) for better triaging.

5. **Integration with CI/CD**
   - Add optional CI jobs that build the instrumented variant and run targeted tests to catch silent regressions.
   - Produce artifacts that include per-thread debug logs and summarized "last statement" reports for easier download from CI runs.

6. **Cross-Platform Parity**
   - Extend tooling support for Windows (MSVC) builds by ensuring the instrumentation rewriter handles `.vcxproj` compile commands or uses clang-cl compatibility layers.
   - Confirm instrumentation runtime works when `ASCII_CHAT_USE_MIMALLOC` or MUSL builds are enabled, documenting any special steps.

7. **Community / External Tooling**
   - Package the instrumentation tool as a standalone utility (future repo) so other projects can reuse it with minimal integration.
   - Provide example patches/scripts for popular build systems (meson, bazel) to ease adoption beyond ascii-chat.
