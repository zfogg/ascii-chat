#!/usr/bin/env bash
set -eu
set -o pipefail 2>/dev/null || true

is_wsl_env() {
  if grep -qi "microsoft" /proc/version 2>/dev/null; then
    return 0
  fi
  return 1
}

is_windows_env() {
  if is_wsl_env; then
    return 0
  fi
  case "${OSTYPE:-}" in
    msys* | mingw* | cygwin* | win32*)
      return 0
      ;;
  esac
  if uname -s 2>/dev/null | grep -qi "mingw\|msys\|cygwin"; then
    return 0
  fi
  return 1
}

normalize_path() {
  local input="$1"
  if [[ -z "${input}" ]]; then
    printf '%s' ""
    return
  fi

  if is_wsl_env && [[ "${input}" =~ ^[A-Za-z]: ]]; then
    if command -v wslpath >/dev/null 2>&1; then
      wslpath -a "${input}"
      return
    fi
  fi

  local sanitized="${input//\\//}"
  sanitized="${sanitized%$'\r'}"
  printf '%s\n' "${sanitized}"
}

to_windows_path() {
  local input="$1"
  if [[ -z "${input}" ]]; then
    printf '%s' ""
    return
  fi
  if is_wsl_env; then
    if command -v wslpath >/dev/null 2>&1; then
      local converted
      converted="$(wslpath -w "${input}" 2>/dev/null || true)"
      converted="${converted%$'\r'}"
      printf '%s\n' "${converted}"
      return
    fi
  fi
  printf '%s\n' "${input}"
}

# Ensure POSIX toolchain precedence on Windows environments
if is_windows_env; then
  PATH="/usr/bin:/bin:/mingw64/bin:/mingw32/bin:$PATH"
  export PATH
fi

show_usage() {
  cat <<'EOF'
Usage: run_defer.sh -b <build-dir> -o <output-dir> [-- <extra clang-tool args...>]

Driver for the defer transformation tool.

Options:
  -b <build-dir>   Path to the CMake build directory containing compile_commands.json (default: ./build)
  -o <output-dir>  Destination directory for defer-transformed sources (must be empty or not exist)
  -h               Show this help message

Environment variables:
  ASCIICHAT_DEFER_TOOL  Override path to the ascii-instr-defer executable

Examples:
  run_defer.sh -b build -o build/defer_transformed
  run_defer.sh -b build -o /tmp/ascii-defer -- lib/server.c src/main.c
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

TOOL_PATH="${ASCIICHAT_DEFER_TOOL:-}"
if [[ -z "${TOOL_PATH}" ]]; then
  TOOL_PATH="${BUILD_DIR}/bin/ascii-instr-defer"
fi

BUILD_DIR="$(normalize_path "${BUILD_DIR}")"
OUTPUT_DIR="$(normalize_path "${OUTPUT_DIR}")"
TOOL_PATH="$(normalize_path "${TOOL_PATH}")"

BUILD_DIR_WIN="$(to_windows_path "${BUILD_DIR}")"
OUTPUT_DIR_WIN="$(to_windows_path "${OUTPUT_DIR}")"
INPUT_ROOT_WIN="$(to_windows_path "${PWD}")"

if is_wsl_env && [[ "${TOOL_PATH}" =~ ^/mnt/[a-z]/ ]] && [[ ! "${TOOL_PATH}" =~ \.exe$ ]]; then
  if [[ -e "${TOOL_PATH}.exe" ]]; then
    TOOL_PATH="${TOOL_PATH}.exe"
  fi
fi

if [[ ! -x "${TOOL_PATH}" ]]; then
  echo "Error: ascii-instr-defer not found or not executable at '${TOOL_PATH}'" >&2
  exit 1
fi

if [[ ! -d "${BUILD_DIR}" ]]; then
  echo "Error: build directory '${BUILD_DIR}' does not exist" >&2
  exit 1
fi

# Use the original compilation database (without defer transformation) for the defer tool
COMPILE_COMMANDS_ORIG="${BUILD_DIR}/compile_commands_original_defer.json"
if [[ -f "${COMPILE_COMMANDS_ORIG}" ]]; then
  cp "${COMPILE_COMMANDS_ORIG}" "${BUILD_DIR}/compile_commands.json"
  mkdir -p "${BUILD_DIR}/compile_db_temp_defer" 2>/dev/null || true
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

OUTPUT_DIR_ABS=$(cd "${OUTPUT_DIR}" && pwd)

for item in *; do
  if [[ -z "$item" ]] || [[ "$item" == "." ]] || [[ "$item" == ".." ]]; then
    continue
  fi

  if [[ "$item" == "build" ]] || [[ "$item" == "build"* ]] || [[ "$item" == ".git" ]] || [ -f "$item"/CMakeCache.txt ]; then
    continue
  fi

  if [[ -d "$item" ]]; then
    item_abs=$(cd "$item" && pwd 2>/dev/null || echo "")
    if [[ -n "$item_abs" ]] && [[ "$OUTPUT_DIR_ABS" == "$item_abs"* ]]; then
      continue
    fi
  fi

  if [[ "$item" == "." ]] || [[ "$item" == ".." ]] || [[ "$item" == "./"* ]]; then
    continue
  fi

  if [[ "$item" == "$(basename "${OUTPUT_DIR}")" ]]; then
    echo "  Skipping $item (output directory)"
    continue
  fi

  echo "  Copying $item..."
  if [ -e "$item" ]; then
    cp -r "$item" "${OUTPUT_DIR}" 2>&1 || true
  else
    echo "  WARNING: $item does not exist, skipping" >&2
  fi
done

rm -rf "${OUTPUT_DIR}/deps/bearssl/build" 2>/dev/null || true
rm -rf "${OUTPUT_DIR}/deps/mimalloc/build" 2>/dev/null || true

# Remove source files (they'll be replaced by transformed versions or copied selectively)
find "${OUTPUT_DIR}" -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.m' -o -name '*.mm' \) -delete 2>/dev/null || true

# Remove headers - they'll be copied after transformation
find "${OUTPUT_DIR}" -type f -name '*.h' -delete 2>/dev/null || true

rm -f "${OUTPUT_DIR}/cmake/tooling/run_defer.sh" 2>/dev/null || true

echo "Source tree copied (excluding source files, headers, and build artifacts)"

# Copy version.h to the transformed tree
if [[ -f "${BUILD_DIR}/generated/version.h" ]]; then
  mkdir -p "${OUTPUT_DIR}/lib"
  cp "${BUILD_DIR}/generated/version.h" "${OUTPUT_DIR}/lib/version.h"
  echo "Copied version.h to transformed tree"
fi

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
  BUILD_DIRS=()
  while IFS= read -r -d '' cache_file; do
    build_dir=$(dirname "${cache_file}")
    BUILD_DIRS+=("${build_dir}")
  done < <(find . -type f -name 'CMakeCache.txt' -print0 2>/dev/null)

  EXCLUDE_ARGS=()
  EXCLUDE_ARGS+=(\( -path 'lib/debug' -o -path 'lib/debug/*' -o -path 'lib/tooling' -o -path 'lib/tooling/*' -o)
  EXCLUDE_ARGS+=(-path 'lib/tests' -o -path 'lib/tests/*' -o)
  EXCLUDE_ARGS+=(-path 'src/tooling' -o -path 'src/tooling/*')

  if is_windows_env; then
    EXCLUDE_ARGS+=(-o -path 'lib/platform/posix' -o -path 'lib/platform/posix/*')
    EXCLUDE_ARGS+=(-o -path 'lib/os/macos' -o -path 'lib/os/macos/*')
    EXCLUDE_ARGS+=(-o -path 'lib/os/linux' -o -path 'lib/os/linux/*')
  else
    EXCLUDE_ARGS+=(-o -path 'lib/platform/windows' -o -path 'lib/platform/windows/*')
    EXCLUDE_ARGS+=(-o -path 'lib/os/windows' -o -path 'lib/os/windows/*')
  fi

  for build_dir in "${BUILD_DIRS[@]}"; do
    EXCLUDE_ARGS+=(-o -path "${build_dir}" -o -path "${build_dir}/*")
  done
  EXCLUDE_ARGS+=(\) -prune -o)

  while IFS= read -r -d '' file; do
    SOURCE_PATHS+=("${file}")
  done < <(find lib src "${EXCLUDE_ARGS[@]}" \
    -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.m' -o -name '*.mm' \) -print0 2>/dev/null)
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
  lib/debug/lock.c | \
  lib/platform/system.c | \
  lib/platform/posix/system.c | \
  lib/platform/posix/mutex.c | \
  lib/platform/posix/thread.c | \
  lib/platform/windows/system.c | \
  lib/platform/windows/mutex.c | \
  lib/platform/windows/thread.c | \
  lib/image2ascii/simd/ascii_simd.c | \
  lib/image2ascii/simd/ascii_simd_color.c | \
  lib/image2ascii/simd/common.c | \
  lib/image2ascii/simd/avx2.c | \
  lib/image2ascii/simd/sse2.c | \
  lib/image2ascii/simd/ssse3.c | \
  lib/image2ascii/simd/neon.c | \
  lib/image2ascii/simd/sve.c | \
  lib/tooling/defer/defer.c)
    continue
    ;;
  esac
  filtered_paths+=("$path")
done
SOURCE_PATHS=("${filtered_paths[@]}")

# Incremental build: filter out unchanged files
CHANGED_SOURCES=()
SKIPPED_COUNT=0

for source_path in "${SOURCE_PATHS[@]}"; do
  if [[ "${source_path}" = /* ]]; then
    abs_source="${source_path}"
  else
    abs_source="${PWD}/${source_path}"
  fi

  transformed_file="${OUTPUT_DIR}/${source_path}"

  if [[ -f "${transformed_file}" ]] && [[ "${transformed_file}" -nt "${abs_source}" ]]; then
    ((SKIPPED_COUNT++))
    continue
  fi

  CHANGED_SOURCES+=("${source_path}")
done

if [[ ${#CHANGED_SOURCES[@]} -eq 0 ]]; then
  echo "All ${#SOURCE_PATHS[@]} source files are up to date, skipping defer transformation"
else
  echo "Transforming ${#CHANGED_SOURCES[@]} changed files with defer transformation (${SKIPPED_COUNT} unchanged, skipped)"

  # Detect number of CPU cores
  NPROC=4  # Default fallback

  if [[ -n "${NUMBER_OF_PROCESSORS:-}" ]]; then
    NPROC="${NUMBER_OF_PROCESSORS}"
  elif command -v nproc >/dev/null 2>&1; then
    NPROC=$(nproc)
  elif [[ -f /proc/cpuinfo ]]; then
    NPROC=$(grep -c ^processor /proc/cpuinfo 2>/dev/null || echo 4)
  elif command -v sysctl >/dev/null 2>&1; then
    NPROC=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
  fi

  PARALLEL_JOBS=$((NPROC * 3 / 4))
  [[ ${PARALLEL_JOBS} -lt 1 ]] && PARALLEL_JOBS=1

  echo "Using ${PARALLEL_JOBS} parallel jobs (${NPROC} CPU cores detected)"

  EXTRA_ARGS=()
  if [[ $# -gt 0 ]]; then
    EXTRA_ARGS+=("$@")
  fi

  # Process files in parallel using xargs
  if command -v xargs >/dev/null 2>&1; then
    printf '%s\n' "${CHANGED_SOURCES[@]}" | xargs -P "${PARALLEL_JOBS}" -I {} \
      "${TOOL_PATH}" -p "${BUILD_DIR_WIN}" --output-dir "${OUTPUT_DIR_WIN}" --input-root "${INPUT_ROOT_WIN}" {} "${EXTRA_ARGS[@]}"
    TRANSFORM_EXIT=$?
  else
    echo "Warning: xargs not found, falling back to sequential processing"
    CMD=("${TOOL_PATH}" -p "${BUILD_DIR_WIN}" --output-dir "${OUTPUT_DIR_WIN}" --input-root "${INPUT_ROOT_WIN}")
    CMD+=("${CHANGED_SOURCES[@]}")
    CMD+=("${EXTRA_ARGS[@]}")
    "${CMD[@]}"
    TRANSFORM_EXIT=$?
  fi

  if [[ ${TRANSFORM_EXIT} -ne 0 ]]; then
    echo "Defer transformation failed with exit code ${TRANSFORM_EXIT}"
    exit ${TRANSFORM_EXIT}
  fi
fi

echo "Defer transformation complete. Now copying headers to transformed tree..."
# Copy all headers to transformed tree AFTER transformation
find "${PWD}" -type f -name '*.h' \
  ! -path "*/build/*" \
  ! -path "*/build_*/*" \
  ! -path "*/.git/*" \
  ! -path "*/.deps-cache/*" \
  ! -path "*/.deps-cache-docker/*" \
  ! -path "*/deps/bearssl/build/*" \
  ! -path "*/deps/mimalloc/build/*" \
  -print0 2>/dev/null | while IFS= read -r -d '' header_file; do
  rel_path="${header_file#${PWD}/}"
  dest_file="${OUTPUT_DIR}/${rel_path}"
  dest_dir="$(dirname "${dest_file}")"
  mkdir -p "${dest_dir}"
  cp "${header_file}" "${dest_file}"
done

echo "Headers copied to transformed tree"

echo "Copying untouched source files to transformed tree..."
find "${PWD}/lib" "${PWD}/src" \
  -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.m' -o -name '*.mm' \) \
  -print0 2>/dev/null | while IFS= read -r -d '' source_file; do
  rel_path="${source_file#${PWD}/}"
  dest_file="${OUTPUT_DIR}/${rel_path}"
  if [[ -f "${dest_file}" ]]; then
    continue
  fi
  dest_dir="$(dirname "${dest_file}")"
  mkdir -p "${dest_dir}"
  cp "${source_file}" "${dest_file}"
done

echo "Untouched source files copied"

echo "Defer transformation complete!"
