#!/bin/bash
# Test script to verify ASCII rendering to pipes works correctly
# Tests that ANSI escape sequences don't appear in piped output

set -e

PROJECT_ROOT="/home/zfogg/src/github.com/zfogg/ascii-chat"
BINARY="$PROJECT_ROOT/build/bin/ascii-chat"

echo "==================================================================="
echo "Testing ASCII rendering to pipes (no ANSI escape sequences)"
echo "==================================================================="

# Test 1: Snapshot mode with pipe
echo ""
echo "Test 1: Snapshot mode piped to file"
OUTPUT_FILE="/tmp/test_snapshot_pipe.txt"
"$BINARY" client --snapshot > "$OUTPUT_FILE" 2>&1 || true

# Check for ANSI escape sequences
if grep -q $'\033' "$OUTPUT_FILE"; then
    echo "❌ FAIL: Found ANSI escape sequences in piped output"
    echo "   Sample: $(head -c 100 "$OUTPUT_FILE" | od -c | head -3)"
    exit 1
else
    echo "✅ PASS: No ANSI escape sequences in piped output"
    echo "   Output length: $(wc -c < "$OUTPUT_FILE") bytes"
fi

# Test 2: Snapshot mode with redirection
echo ""
echo "Test 2: Snapshot mode with stderr redirection"
OUTPUT_FILE="/tmp/test_snapshot_redirect.txt"
"$BINARY" client --snapshot 2>&1 > "$OUTPUT_FILE" || true

if grep -q $'\033' "$OUTPUT_FILE"; then
    echo "❌ FAIL: Found ANSI escape sequences in redirected output"
    exit 1
else
    echo "✅ PASS: No ANSI escape sequences in redirected output"
fi

# Test 3: Snapshot mode with cat
echo ""
echo "Test 3: Snapshot mode piped through cat"
OUTPUT_FILE="/tmp/test_snapshot_cat.txt"
"$BINARY" client --snapshot 2>&1 | cat > "$OUTPUT_FILE" || true

if grep -q $'\033' "$OUTPUT_FILE"; then
    echo "❌ FAIL: Found ANSI escape sequences after piping through cat"
    exit 1
else
    echo "✅ PASS: No ANSI escape sequences after piping through cat"
fi

# Test 4: TESTING environment variable
echo ""
echo "Test 4: TESTING environment variable set"
OUTPUT_FILE="/tmp/test_testing_env.txt"
TESTING=1 "$BINARY" client --snapshot > "$OUTPUT_FILE" 2>&1 || true

if grep -q $'\033' "$OUTPUT_FILE"; then
    echo "❌ FAIL: Found ANSI escape sequences with TESTING=1"
    exit 1
else
    echo "✅ PASS: No ANSI escape sequences with TESTING=1"
fi

# Sanity check: verify content is valid ASCII art (contains newlines)
echo ""
echo "Test 5: Verify piped output contains valid ASCII art"
OUTPUT_FILE="/tmp/test_valid_ascii.txt"
"$BINARY" client --snapshot > "$OUTPUT_FILE" 2>&1 || true

LINES=$(wc -l < "$OUTPUT_FILE")
CHARS=$(wc -c < "$OUTPUT_FILE")

if [ "$LINES" -gt 1 ] && [ "$CHARS" -gt 100 ]; then
    echo "✅ PASS: Output contains valid ASCII art ($LINES lines, $CHARS bytes)"
else
    echo "❌ FAIL: Output doesn't look like valid ASCII art ($LINES lines, $CHARS bytes)"
    exit 1
fi

echo ""
echo "==================================================================="
echo "All pipe tests passed! ✅"
echo "==================================================================="
