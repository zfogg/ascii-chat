# Full Source Print Instrumentation Pipeline Plan

## Goals

- [x] Build a Clang libTooling executable that rewrites ascii-chat C sources, inserting per-statement logging calls for the source_print instrumentation pipeline.
- [x] Provide a runtime logging helper that records PID/TID, file, line, macro status, and source snippet via single write() calls with env-configurable filters.
- [x] Wire the tool into CMake so we can compile both original and source_print-instrumented builds easily while guaranteeing the original sources remain untouched.

## Steps

### Runtime Logging Library (lib/tooling/)
- [x] Create `lib/tooling/instrument_log.h` and `lib/tooling/instrument_log.c` implementing `ascii_instr_log_line()` using async-signal-safe `write()`; include PID/TID, timestamp, macro flag, snippet.
- [x] Add env-configurable filters (e.g., `ASCII_INSTR_SOURCE_PRINT_INCLUDE`, `ASCII_INSTR_SOURCE_PRINT_THREAD`, `ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR`) and per-thread file support.
- [x] Expose initialization/teardown helpers for thread-local log files.
- [x] Register the new source files in the existing library target definitions (via `cmake/tooling/Instrumentation.cmake`).

### Source Print Instrumentation Tool (`src/tooling/`)
- [x] Implement `src/tooling/ascii_instr_tool.cpp` using Clang libTooling + AST visitors to insert `ascii_instr_log_line()` before each executable `Stmt`, capturing original source text via `SourceManager`.
- [x] Enforce “write-new-only” safety: validate target output directories, refuse to overwrite existing files, and abort on any attempt to reuse an existing path.
- [x] Handle macros by tagging expansion sites; log metadata distinguishing expansion use.
- [x] Add command-line options for selecting files/functions and for skipping functions marked with `ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER` (via annotate attribute) macro.
- [x] Emit transformed sources to a dedicated output directory mirroring the input tree.

### Build System Integration (`cmake/tooling/`)
- [x] Provide `cmake/tooling/Targets.cmake` that defines the libTooling executable and `ascii-debug-runtime`, ensuring LLVM/Clang discovery happens there.
- [x] Expose `ascii_instrumentation_prepare()` / `ascii_instrumentation_finalize()` helpers in `cmake/tooling/Instrumentation.cmake`, invoked from the root `CMakeLists.txt`.
- [x] Add cache option `ASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION` and integrate `cmake/tooling/run_instrumentation.sh` to generate source_print-instrumented sources safely into `build/instrumented/`.

### Developer Documentation & Usage
- [x] Document workflow in `docs/tooling-instrumentation.md`: configuration, environment filters, per-thread logs.
- [x] Highlight safety guarantees (no overwrites/deletions of source/.git) and recommended manual checks before use.
- [x] Note limitations (performance overhead, sanitizers, signal-handler opt-outs) and post-processing tips.

## Optional Enhancements (time permitting)
- [x] Provide a small post-processing helper in `src/tooling/` that summarizes the last log per thread.
- [ ] Add unit tests (Criterion) for the logging runtime to ensure env filters behave correctly (skipped in macOS CI if needed).

---

## Part Two – Expanded Ideas from NOTES.md

1. **Macro-Aware Logging Enhancements**
   - [x] Instrument both macro invocation sites and the expanded statements, tagging each log with `macro_invocation` / `macro_expansion` markers.
   - [x] Add CLI toggles (`--log-macro-invocations`, `--log-macro-expansions`) to control noise levels.
   - [x] Research `SourceManager::getImmediateMacroCallerLoc()` to improve correlation between invocation and expansion logs.

2. **Advanced Filtering & Noise Control**
   - [x] Extend runtime filters to support regular-expression includes/excludes for files and functions.
   - [x] Add rate limiting (e.g., `ASCII_INSTR_SOURCE_PRINT_RATE=10` to log every Nth statement per thread).
   - [x] Support `ASCII_INSTR_SOURCE_PRINT_ONLY=server:*` to scope logging to module prefixes or glob patterns.

3. **Signal-Safe Postmortem Tooling**
   - [x] Create a small analyzer that parses per-thread log files and reports the last executed statement per TID, highlighting divergence between threads.
   - [ ] Hook into crash handlers (where safe) to emit the last statement snapshot automatically.

4. **SanitizerCoverage Mode (Alternate Path)**
   - [x] Prototype the lightweight `__sanitizer_cov_trace_pc_guard` variant to cross-check crash locations without source rewriting.
   - [ ] Build a symbolizer helper that resolves PCs to `file:line` and optionally prints the on-disk source snippet for parity with the source-to-source path.

5. **Signal Handler Opt-In/Out Support**
   - [x] Implement optional annotation macros (e.g., `#define ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER`) that map to Clang attributes for exclusion.
   - [x] Document guidelines for marking async-signal-safe sections to avoid interference from inserted logging.

6. **Documentation & Examples**
   - [x] Add `docs/tooling-instrumentation.md` with configuration walk-throughs, env var reference, and example workflows (e.g., "instrument only client display code").
   - [x] Provide scripts/snippets for tailing logs, filtering by thread, and mapping macros to expansions.

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

---

## Part Four – Execution Roadmap (Next 2 Sprints)

1. **Finalize Documentation**
   - [x] Complete `docs/tooling-instrumentation.md` with end-to-end workflow, env var reference, and troubleshooting.
   - [ ] Record a short asciinema or GIF walkthrough demonstrating instrumentation on a simple crash.
2. **Stabilize Build Integration**
   - [ ] Add CI job that configures with `-DASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION=ON` to validate the pipeline builds on macOS/Linux.
   - [ ] Create a convenience script (`scripts/instrumented-build.sh`) that wraps preset configuration, emphasizing no overwrite guarantees.
3. **Runtime Validation**
   - [ ] Write deterministic unit tests covering filter combinations (include/exclude, rate, thread selection) using mock log sinks.
   - [ ] Stress-test per-thread logging on a synthetic multi-thread workload and document sustained throughput limits.
4. **Developer Onboarding Materials**
   - [ ] Publish a sample workflow showing how to diff instrumented vs. non-instrumented timing to gauge overhead.
   - [ ] Add FAQ entries addressing common integration questions (macros, signal handlers, sanitizer interplay).

---

## Part Five – Risks, Mitigations, and Success Metrics

1. **Risk: Excessive Overhead in Hot Paths**
   - *Mitigation:* Encourage scoped instrumentation via filters; document rate limiting defaults and provide profiling guidance.
   - *Metric:* Instrumented build should remain within 5× runtime of baseline for targeted modules.
2. **Risk: Log Volume Overwhelms Storage or Pipelines**
   - *Mitigation:* Default to per-thread rolling logs with size caps; expose `ASCII_INSTR_SOURCE_PRINT_MAX_BYTES` env var; integrate optional gzip rotation.
   - *Metric:* Demonstrate a 10-minute instrumented session stays under 500 MB with defaults.
3. **Risk: Developer Misuse in Signal Handlers**
   - *Mitigation:* Enforce compiler warnings when `ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER` functions are instrumented; highlight safe patterns in docs.
   - *Metric:* Zero known incidents of instrumented signal handlers in postmortems.
4. **Risk: Divergence Between Original and Instrumented Trees**
   - *Mitigation:* Add automated sanity checks comparing hashes of non-instrumented files; gate merges on clean diffs.
   - *Metric:* CI guardrail that fails if non-instrumented sources are mutated by the pipeline.
5. **Success Criteria**
   - First production bug localized primarily via instrumentation logs.
   - Positive developer feedback captured in `notes/IMPROVEMENTS.md` (at least two entries).
   - Optional sanitizercov mode adopted for nightly fuzzing runs with documented wins.
