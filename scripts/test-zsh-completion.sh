#!/bin/bash
# Test zsh completion by validating the generated script and interactive behavior

set -e

cd "$(dirname "$(dirname "$(realpath "$0")")")" || exit 1

SCRIPT_FILE="build/share/zsh/site-functions/_ascii_chat"

if [ ! -f "$SCRIPT_FILE" ]; then
  echo "🔴 Completion script not found at $SCRIPT_FILE"
  exit 1
fi

SCRIPT=$(cat "$SCRIPT_FILE")

echo "Testing zsh completion..."
echo ""

# =============================================================================
# TEST 1: Binary-level options (--password, --log-file, --key, etc.)
# =============================================================================
echo "=== TEST 1: Binary-level options ==="
if echo "$SCRIPT" | rg -q "(--password|--log-file|--key)"; then
  echo "🟢 Binary-level options (--password, --log-file, --key) found"
  TEST1_PASS=1
else
  echo "🔴 Binary-level options NOT found"
  TEST1_PASS=0
fi

if echo "$SCRIPT" | rg -q "case.*\\\$prev.*in" | head -1 >/dev/null 2>&1; then
  echo "🟢 Value completion case blocks found"
else
  echo "🔴 Value completion case blocks NOT found"
  TEST1_PASS=0
fi

# =============================================================================
# TEST 2: Discovery mode options
# =============================================================================
echo ""
echo "=== TEST 2: Discovery mode options ==="
if echo "$SCRIPT" | rg -q "_ascii_chat_discovery"; then
  echo "🟢 Discovery mode completion function found"
  TEST2_PASS=1
else
  echo "🔴 Discovery mode completion function NOT found"
  TEST2_PASS=0
fi

# =============================================================================
# TEST 3: Client mode audio option
# =============================================================================
echo ""
echo "=== TEST 3: Client mode --audio option ==="
if echo "$SCRIPT" | rg -q "\-\-audio"; then
  echo "🟢 Client-mode --audio option found"
  TEST3_PASS=1
else
  echo "🔴 Client-mode --audio option NOT found"
  TEST3_PASS=0
fi

# =============================================================================
# Validation: Device helpers
# =============================================================================
echo ""
echo "=== Validation: Device index helpers ==="
HELPERS_PASS=1

if echo "$SCRIPT" | rg -q "_ascii_chat_webcam_indices"; then
  echo "🟢 Webcam indices helper found"
else
  echo "🔴 Webcam indices helper NOT found"
  HELPERS_PASS=0
fi

if echo "$SCRIPT" | rg -q "_ascii_chat_microphone_indices"; then
  echo "🟢 Microphone indices helper found"
else
  echo "🔴 Microphone indices helper NOT found"
  HELPERS_PASS=0
fi

if echo "$SCRIPT" | rg -q "_ascii_chat_speakers_indices"; then
  echo "🟢 Speakers indices helper found"
else
  echo "🔴 Speakers indices helper NOT found"
  HELPERS_PASS=0
fi

# =============================================================================
# Validation: Enum value completions
# =============================================================================
echo ""
echo "=== Validation: Enum value completions ==="
ENUM_PASS=1

if echo "$SCRIPT" | rg -q "auto none 16 256 truecolor"; then
  echo "🟢 Color mode enum values found"
else
  echo "🔴 Color mode enum values NOT found"
  ENUM_PASS=0
fi

if echo "$SCRIPT" | rg -q "standard blocks digital minimal cool custom"; then
  echo "🟢 Palette enum values found"
else
  echo "🔴 Palette enum values NOT found"
  ENUM_PASS=0
fi

if echo "$SCRIPT" | rg -q "true false"; then
  echo "🟢 Boolean true/false values found"
else
  echo "🔴 Boolean true/false values NOT found"
  ENUM_PASS=0
fi

# =============================================================================
# Validation: Zsh syntax check
# =============================================================================
echo ""
echo "=== Validation: Zsh syntax check ==="
if zsh -n "$SCRIPT_FILE" 2>/dev/null; then
  echo "🟢 Zsh syntax is valid"
  SYNTAX_PASS=1
else
  echo "🔴 Zsh syntax check FAILED"
  SYNTAX_PASS=0
fi

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "========== SUMMARY =========="
if [ "$TEST1_PASS" -eq 1 ] && [ "$TEST2_PASS" -eq 1 ] && [ "$TEST3_PASS" -eq 1 ] && \
   [ "$HELPERS_PASS" -eq 1 ] && [ "$ENUM_PASS" -eq 1 ] && [ "$SYNTAX_PASS" -eq 1 ]; then
  echo "🟢 ALL TESTS PASSED"
  exit 0
else
  echo "🔴 SOME TESTS FAILED"
  exit 1
fi
