# Full Instrumentation Pipeline Plan


## Goals

Build a Clang libTooling executable that rewrites ascii-chat C sources, inserting per-statement logging calls.
Provide a runtime logging helper that records PID/TID, file, line, macro status, and source snippet via single write() calls with env-configurable filters.
Wire the tool into CMake so we can compile both original and instrumented builds easily while guaranteeing the original sources remain untouched.


## Steps

Runtime Logging Library (lib/debug/)
Create lib/debug/instrument_log.h and lib/debug/instrument_log.c implementing ascii_instr_log_line() using async-signal-safe write(); include PID/TID, timestamp, macro flag, snippet.
Add env-configurable filters (e.g., ASCII_INSTR_INCLUDE, ASCII_INSTR_THREAD, ASCII_INSTR_OUTPUT_DIR) and per-thread file support.
Expose initialization/teardown helpers for thread-local log files.
Register the new source files in the existing library target definitions (within cmake/targets/Libraries.cmake).
Instrumentation Tool (src/debug/)
Implement src/debug/ascii_instr_tool.cpp using Clang libTooling + ASTMatchers to insert ascii_instr_log_line() before each executable Stmt, capturing original source text via SourceManager.
Enforce “write-new-only” safety: validate target output directories, refuse to overwrite existing files, and abort on any attempt to reuse an existing path.
Handle macros by logging both invocation site and expanded statement when debug info allows; tag logs with macro metadata.
Add command-line options for selecting files/functions and for skipping functions marked with ASCII_INSTR_SIGNAL_HANDLER macro.
Emit transformed sources to a dedicated output directory mirroring the input tree.
Build System Integration (cmake/debug/)
Create cmake/debug/CMakeLists.txt that defines the libTooling executable and ascii-debug-runtime runtime target, ensuring LLVM/Clang discovery happens here.
Include this subdirectory from the root CMakeLists.txt so the tooling can be built via the main project (e.g., cmake --build build --target ascii-instr-tool).
Provide a cache option ASCII_BUILD_WITH_INSTRUMENTATION that, when enabled, rewrites sources into build/instrumented/ (never touching originals) and compiles the instrumented tree linking against ascii-debug-runtime.
Add a convenience script cmake/debug/run_instrumentation.sh to invoke the tool across src/ and lib/, with guard rails against deleting or overwriting existing source trees.
Developer Documentation & Usage
Document workflow in docs/debug-instrumentation.md: how to configure, run, filter by env vars, and locate per-thread logs.
Highlight the safety guarantees (no overwrite, no deletion of source/.git) and expected manual checks before using in active branches.
Mention limitations (performance, sanitizers, signal-handler opt-outs) and recommendations for tailing logs and post-processing.


## Optional Enhancements (time permitting)

Provide a small post-processing helper in src/debug/ that summarizes last log per thread.
Add unit tests (Criterion) for the logging runtime to ensure env filters behave correctly (skipped in macOS CI if needed).
