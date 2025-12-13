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

# When running under Bazel, the tool is provided via runfiles
# Check for runfiles location first (Bazel sandbox)
if [[ -n "${RUNFILES_DIR}" ]]; then
    DEFER_TOOL="${RUNFILES_DIR}/_main/.deps-cache/defer-tool/ascii-instr-defer"
    if [[ -x "${DEFER_TOOL}" ]]; then
        exec "${DEFER_TOOL}" "$@"
    fi
fi

# Try relative to script location for Bazel
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# In Bazel, try going up to find .deps-cache
for i in 1 2 3 4 5 6; do
    CANDIDATE="${SCRIPT_DIR}/.deps-cache/defer-tool/ascii-instr-defer"
    if [[ -x "${CANDIDATE}" ]]; then
        exec "${CANDIDATE}" "$@"
    fi
    SCRIPT_DIR="$(dirname "${SCRIPT_DIR}")"
done

# Fallback: try common locations
REPO_ROOT="/home/zfogg/src/github.com/zfogg/ascii-chat"
DEFER_TOOL=""

if [[ -x "${REPO_ROOT}/.deps-cache/defer-tool/ascii-instr-defer" ]]; then
    DEFER_TOOL="${REPO_ROOT}/.deps-cache/defer-tool/ascii-instr-defer"
elif [[ -x "${REPO_ROOT}/.deps-cache/Debug/defer-tool/ascii-instr-defer" ]]; then
    DEFER_TOOL="${REPO_ROOT}/.deps-cache/Debug/defer-tool/ascii-instr-defer"
elif [[ -x "${REPO_ROOT}/build/ascii-instr-defer" ]]; then
    DEFER_TOOL="${REPO_ROOT}/build/ascii-instr-defer"
fi

if [[ -z "${DEFER_TOOL}" ]]; then
    echo "Error: ascii-instr-defer tool not found." >&2
    echo "Build it with CMake first:" >&2
    echo "  cmake --preset debug -B build && cmake --build build" >&2
    exit 1
fi

exec "${DEFER_TOOL}" "$@"
