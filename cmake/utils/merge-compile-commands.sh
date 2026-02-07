#!/bin/bash
# =============================================================================
# Merge compile_commands.json from WASM build into main build
# =============================================================================
# Uses jq to properly merge JSON arrays from both builds.
#
# Usage:
#   ./merge-compile-commands.sh <main> <wasm> <output>
# =============================================================================

set -e

MAIN_JSON="$1"
WASM_JSON="$2"
OUTPUT_JSON="$3"

if [[ ! -f "$MAIN_JSON" ]]; then
    echo "Error: Main compile_commands.json not found: $MAIN_JSON" >&2
    exit 1
fi

if [[ ! -f "$WASM_JSON" ]]; then
    echo "Warning: WASM compile_commands.json not found: $WASM_JSON" >&2
    echo "Copying main compile_commands.json without WASM entries" >&2
    cp "$MAIN_JSON" "$OUTPUT_JSON"
    exit 0
fi

# Check if jq is available
if ! command -v jq &> /dev/null; then
    echo "Error: jq not found. Please install jq to merge compile_commands.json" >&2
    exit 1
fi

# Merge arrays: concatenate main + wasm
jq -s '.[0] + .[1]' "$MAIN_JSON" "$WASM_JSON" > "$OUTPUT_JSON"

echo "Merged compile_commands.json: $OUTPUT_JSON ($(jq length "$OUTPUT_JSON") entries)"
