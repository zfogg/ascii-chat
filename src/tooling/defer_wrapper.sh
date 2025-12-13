#!/bin/bash
# =============================================================================
# Wrapper script for ascii-instr-defer tool
# =============================================================================
# Uses the pre-built defer tool from CMake build or .deps-cache
#
# Build the tool with CMake:
#   cmake --preset debug -B build && cmake --build build
# =============================================================================

# Disable sanitizers at runtime - the defer tool was built with sanitizers
# but triggers UBSan errors in LLVM code that are not actionable
export ASAN_OPTIONS="detect_leaks=0:halt_on_error=0"
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=0"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# When running under Bazel with runfiles
if [[ -n "${RUNFILES_DIR}" ]]; then
    DEFER_TOOL="${RUNFILES_DIR}/_main/.deps-cache/defer-tool/ascii-instr-defer"
    if [[ -x "${DEFER_TOOL}" ]]; then
        exec "${DEFER_TOOL}" "$@"
    fi
fi

# Check in execroot (Bazel sandbox runs from here)
if [[ -x ".deps-cache/defer-tool/ascii-instr-defer" ]]; then
    exec ".deps-cache/defer-tool/ascii-instr-defer" "$@"
fi

# Search upward from script location
SEARCH_DIR="${SCRIPT_DIR}"
for i in 1 2 3 4 5 6 7 8; do
    if [[ -x "${SEARCH_DIR}/.deps-cache/defer-tool/ascii-instr-defer" ]]; then
        exec "${SEARCH_DIR}/.deps-cache/defer-tool/ascii-instr-defer" "$@"
    fi
    SEARCH_DIR="$(dirname "${SEARCH_DIR}")"
done

echo "Error: ascii-instr-defer tool not found." >&2
echo "Build it with CMake first:" >&2
echo "  cmake --preset debug -B build && cmake --build build" >&2
exit 1
