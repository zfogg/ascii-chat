#!/usr/bin/env bash
set -euo pipefail

show_usage() {
  cat <<'EOF'
Usage: run_instrumentation.sh -b <build-dir> -o <output-dir> [-- <extra clang-tool args...>]

Options:
  -b <build-dir>   Path to the CMake build directory containing compile_commands.json (default: ./build)
  -o <output-dir>  Destination directory for instrumented sources (must be empty or not exist)
  -h               Show this help message

Environment variables:
  ASCII_INSTR_TOOL  Override path to the ascii-instr-tool executable

Examples:
  run_instrumentation.sh -b build -o build/instrumented
  run_instrumentation.sh -b build -o /tmp/ascii-instrumented -- lib/server.c src/main.c
EOF
}

BUILD_DIR=""
OUTPUT_DIR=""

while getopts ":b:o:h" opt; do
  case "${opt}" in
  b)
    BUILD_DIR="${OPTARG}"
    ;;
  o)
    OUTPUT_DIR="${OPTARG}"
    ;;
  h)
    show_usage
    exit 0
    ;;
  :)
    echo "Missing argument for -${OPTARG}" >&2
    show_usage >&2
    exit 1
    ;;
  ?)
    echo "Unknown option: -${OPTARG}" >&2
    show_usage >&2
    exit 1
    ;;
  esac
done
shift $((OPTIND - 1))

if [[ -z "${OUTPUT_DIR}" ]]; then
  echo "Error: output directory (-o) is required" >&2
  show_usage >&2
  exit 1
fi

if [[ -z "${BUILD_DIR}" ]]; then
  BUILD_DIR="${PWD}/build"
fi

TOOL_PATH="${ASCII_INSTR_TOOL:-}"
if [[ -z "${TOOL_PATH}" ]]; then
  TOOL_PATH="${BUILD_DIR}/bin/ascii-instr-tool"
fi

if [[ ! -x "${TOOL_PATH}" ]]; then
  echo "Error: ascii-instr-tool not found or not executable at '${TOOL_PATH}'" >&2
  exit 1
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "Error: build directory '${BUILD_DIR}' does not exist" >&2
  exit 1
fi

COMPILE_COMMANDS="${BUILD_DIR}/compile_commands.json"
if [[ ! -f "${COMPILE_COMMANDS}" ]]; then
  echo "Error: compile_commands.json not found in '${BUILD_DIR}'. Run CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 1
fi

if [[ -e "${OUTPUT_DIR}" ]]; then
  if [[ ! -d "${OUTPUT_DIR}" ]]; then
    echo "Error: output path '${OUTPUT_DIR}' exists and is not a directory" >&2
    exit 1
  fi
  if find "${OUTPUT_DIR}" -mindepth 1 -print -quit | grep -q .; then
    echo "Error: output directory '${OUTPUT_DIR}' must be empty" >&2
    exit 1
  fi
else
  mkdir -p "${OUTPUT_DIR}"
fi

# Copy directory structure and non-source files to output directory
echo "Copying source tree to ${OUTPUT_DIR}..."

# Get absolute path of output directory to avoid copying into itself
OUTPUT_DIR_ABS=$(cd "${OUTPUT_DIR}" && pwd)

# Copy each top-level item except build directories and .git
for item in *; do
  # Skip empty, '.', '..' entries
  if [[ -z "$item" ]] || [[ "$item" == "." ]] || [[ "$item" == ".." ]]; then
    continue
  fi

  # Skip build directories, .git, and the output directory itself
  if [[ "$item" == "build" ]] || [[ "$item" == "build"* ]] || [[ "$item" == ".git" ]] || [ -f "$item"/CMakeCache.txt ]; then
    continue
  fi

  # Skip if this item contains the output directory
  if [[ -d "$item" ]]; then
    item_abs=$(cd "$item" && pwd 2>/dev/null || echo "")
    if [[ -n "$item_abs" ]] && [[ "$OUTPUT_DIR_ABS" == "$item_abs"* ]]; then
      continue
    fi
  fi

  # Extra safety check - never copy current/parent directory
  if [[ "$item" == "." ]] || [[ "$item" == ".." ]] || [[ "$item" == "./"* ]]; then
    continue
  fi

  echo "  Copying $item..."
  cp -r "$item" "${OUTPUT_DIR}/" 2>&1 || true
done

# Remove additional unwanted build directories if they got copied
rm -rf "${OUTPUT_DIR}/deps/bearssl/build" 2>/dev/null || true
rm -rf "${OUTPUT_DIR}/deps/mimalloc/build" 2>/dev/null || true

# Remove source files (they'll be replaced by instrumented versions)
find "${OUTPUT_DIR}" -type f \( -name '*.c' -o -name '*.m' -o -name '*.mm' \) -delete 2>/dev/null || true

# Remove this script itself
rm -f "${OUTPUT_DIR}/cmake/debug/run_instrumentation.sh" 2>/dev/null || true

echo "Source tree copied (excluding source files and build artifacts)"

declare -a SOURCE_PATHS=()
if [[ $# -gt 0 ]]; then
  while [[ $# -gt 0 ]]; do
    if [[ "$1" == "--" ]]; then
      shift
      break
    fi
    SOURCE_PATHS+=("$1")
    shift
  done
fi

if [[ ${#SOURCE_PATHS[@]} -eq 0 ]]; then
  # Find all CMake build directories (directories containing CMakeCache.txt)
  BUILD_DIRS=()
  while IFS= read -r -d '' cache_file; do
    build_dir=$(dirname "${cache_file}")
    BUILD_DIRS+=("${build_dir}")
  done < <(find . -type f -name 'CMakeCache.txt' -print0 2>/dev/null)

  # Build exclusion patterns for find command
  EXCLUDE_ARGS=()
  # Exclude debug and test directories
  EXCLUDE_ARGS+=(\( -path 'lib/debug' -o -path 'lib/debug/*' -o)
  EXCLUDE_ARGS+=(-path 'lib/tests' -o -path 'lib/tests/*' -o)
  EXCLUDE_ARGS+=(-path 'src/debug' -o -path 'src/debug/*')

  # Exclude platform-specific directories that don't match current platform
  # Detect OS
  if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]] || uname -s | grep -qi "mingw\|msys"; then
    # On Windows: exclude POSIX-specific directories
    EXCLUDE_ARGS+=(-o -path 'lib/platform/posix' -o -path 'lib/platform/posix/*')
    EXCLUDE_ARGS+=(-o -path 'lib/os/macos' -o -path 'lib/os/macos/*')
    EXCLUDE_ARGS+=(-o -path 'lib/os/linux' -o -path 'lib/os/linux/*')
  else
    # On POSIX: exclude Windows-specific directories
    EXCLUDE_ARGS+=(-o -path 'lib/platform/windows' -o -path 'lib/platform/windows/*')
    EXCLUDE_ARGS+=(-o -path 'lib/os/windows' -o -path 'lib/os/windows/*')
  fi

  # Exclude all CMake build directories
  for build_dir in "${BUILD_DIRS[@]}"; do
    EXCLUDE_ARGS+=(-o -path "${build_dir}" -o -path "${build_dir}/*")
  done
  EXCLUDE_ARGS+=(\) -prune -o)

  # Find all source files, excluding the directories above
  while IFS= read -r -d '' file; do
    SOURCE_PATHS+=("${file}")
  done < <(find lib src "${EXCLUDE_ARGS[@]}" \
    -type f \( -name '*.c' -o -name '*.m' -o -name '*.mm' \) -print0 2>/dev/null)
fi

declare -A SEEN_SOURCE_PATHS=()
deduped_paths=()
for path in "${SOURCE_PATHS[@]}"; do
  if [[ -n "${SEEN_SOURCE_PATHS[$path]:-}" ]]; then
    continue
  fi
  SEEN_SOURCE_PATHS["$path"]=1
  deduped_paths+=("$path")
done
SOURCE_PATHS=("${deduped_paths[@]}")

filtered_paths=()
for path in "${SOURCE_PATHS[@]}"; do
  case "$path" in
  lib/lock_debug.c | \
  lib/platform/system.c | \
  lib/platform/posix/system.c | \
  lib/platform/posix/mutex.c | \
  lib/platform/posix/thread.c | \
  lib/platform/windows/system.c | \
  lib/platform/windows/mutex.c | \
  lib/platform/windows/thread.c)
    continue
    ;;
  lib/platform/windows/* | lib/platform/windows\\*)
    continue
    ;;
  lib/os/windows/* | lib/os/windows\\*)
    continue
    ;;
  esac
  filtered_paths+=("$path")
done
SOURCE_PATHS=("${filtered_paths[@]}")

CMD=("${TOOL_PATH}" -p "${BUILD_DIR}" --output-dir "${OUTPUT_DIR}" --input-root "${PWD}")
CMD+=("${SOURCE_PATHS[@]}")

if [[ $# -gt 0 ]]; then
  CMD+=("$@")
fi

echo "Running instrumentation tool: ${CMD[*]}"
"${CMD[@]}"

# Headers are not symlinked - the build will use original headers via include paths
# This avoids #pragma once issues with symlinks on Windows
echo "Skipping header symlinking (using original headers via include paths)"

extra_source_links=(
  "lib/platform/system.c"
  "lib/lock_debug.c"
  "lib/platform/posix/system.c"
  "lib/platform/posix/mutex.c"
  "lib/platform/posix/thread.c"
  "lib/platform/windows/system.c"
  "lib/platform/windows/mutex.c"
  "lib/platform/windows/thread.c"
)

for source_path in "${extra_source_links[@]}"; do
  dest="${OUTPUT_DIR}/${source_path}"
  mkdir -p "$(dirname "${dest}")"
  rm -f "${dest}"
  ln -s "${PWD}/${source_path}" "${dest}"
done
