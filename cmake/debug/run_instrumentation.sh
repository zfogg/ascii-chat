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

rsync -a \
    --exclude '.git/' \
    --exclude 'build/' \
    --exclude 'build_instr/' \
    --exclude 'build_instrumented/' \
    --exclude 'build_docker/' \
    --exclude 'deps/bearssl/build/' \
    --exclude 'deps/mimalloc/build/' \
    --exclude '*.c' \
    --exclude '*.m' \
    --exclude '*.mm' \
    --exclude 'cmake/debug/run_instrumentation.sh' \
    ./ "${OUTPUT_DIR}/"


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
    while IFS= read -r -d '' file; do
        SOURCE_PATHS+=("${file}")
    done < <(find lib src \
        \( -path 'lib/debug' -o -path 'lib/debug/*' \) -prune -o \
        -type f \( -name '*.c' -o -name '*.m' -o -name '*.mm' \) -print0)
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
        lib/platform/system.c)
            continue
            ;;
        lib/platform/windows/*|lib/platform/windows\\*)
            continue
            ;;
        lib/os/windows/*|lib/os/windows\\*)
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

echo "Linking header files into instrumented tree..."
find lib src \
    \( -path 'lib/debug' -o -path 'lib/debug/*' \) -prune -o \
    -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.inc' -o -name '*.inl' \) -print0 |
    while IFS= read -r -d '' header; do
        dest="${OUTPUT_DIR}/${header}"
        mkdir -p "$(dirname "${dest}")"
        if [[ -e "${dest}" || -L "${dest}" ]]; then
            rm -f "${dest}"
        fi
        ln -s "${PWD}/${header}" "${dest}"
    done

extra_source_links=(
    "lib/platform/system.c"
)

for source_path in "${extra_source_links[@]}"; do
    dest="${OUTPUT_DIR}/${source_path}"
    mkdir -p "$(dirname "${dest}")"
    rm -f "${dest}"
    ln -s "${PWD}/${source_path}" "${dest}"
done

