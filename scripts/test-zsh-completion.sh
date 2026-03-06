#!/bin/bash
# Test zsh completion interactively in tmux

set -e

cd "$(dirname "$(dirname "$(realpath "$0")")")" || exit 1

echo "Testing zsh completion interactively..."
echo "This will show actual zsh completion in action"
echo ""

# Kill any existing test session
tmux kill-session -t zsh-completion-test 2>/dev/null || true
sleep 0.2

# Create new session with larger dimensions for better visibility
tmux new-session -d -s zsh-completion-test -x 250 -y 60

# Send setup commands
tmux send-keys -t zsh-completion-test "cd '$PWD'" Enter
sleep 0.2

tmux send-keys -t zsh-completion-test "fpath=(./build/share/zsh/site-functions \$fpath); autoload -Uz compinit; compinit -U" Enter
sleep 0.2

tmux send-keys -t zsh-completion-test "clear" Enter
sleep 0.2

# =============================================================================
# TEST 1: Binary-level options (--sho<TAB>)
# =============================================================================
echo "=== TEST 1: ascii-chat --<TAB> ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat --" Tab
sleep 0.5

# Navigate menu to display options - press Down multiple times to scroll through menu and trigger all groups
tmux send-keys -t zsh-completion-test "Down Down Down Down Down"
sleep 0.5

# Capture and save output - capture full history to see all group headers
tmux capture-pane -t zsh-completion-test -p -S -200 > /tmp/test1_output.txt

# Display what we got
echo "Captured output:"
cat /tmp/test1_output.txt | tail -20
echo ""

# Check for success indicators
if cat /tmp/test1_output.txt | rg -q "(--snapshot|--splash|--help|--password)"; then
  echo "🟢 TEST 1 PASSED: Binary-level options showing"
  TEST1_PASS=1
else
  echo "🔴 TEST 1 FAILED: Binary-level options NOT showing"
  TEST1_PASS=0
fi
# Check for discovery mode options (from binary-level completing with discovery options)
if cat /tmp/test1_output.txt | rg -q "audio options"; then
  echo "🟢 TEST 1 PASSED: Discovery mode options showing"
  TEST1_PASS=1
else
  echo "🔴 TEST 1 FAILED: Discovery mode options NOT showing"
  TEST1_PASS=0
fi

# Clear for next test
tmux send-keys -t zsh-completion-test "C-u"
sleep 0.2

# =============================================================================
# TEST 2: Discovery mode options (discovery --<TAB>)
# =============================================================================
echo ""
echo "=== TEST 2: ascii-chat discovery --<TAB> ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat discovery --" Tab
sleep 0.2

# Navigate completion menu with arrow down to see options
tmux send-keys -t zsh-completion-test "Down"
sleep 0.2

# Capture and save output
tmux capture-pane -t zsh-completion-test -p > /tmp/test2_output.txt

# Display what we got
echo "Captured output:"
cat /tmp/test2_output.txt | tail -20
echo ""

# Check for success indicators
if cat /tmp/test2_output.txt | rg -q "(database|port|password|discovery-service)"; then
  echo "🟢 TEST 2 PASSED: Discovery options showing"
  TEST2_PASS=1
else
  echo "🔴 TEST 2 FAILED: Discovery options NOT showing"
  TEST2_PASS=0
fi

# Clear for next test
tmux send-keys -t zsh-completion-test "Escape"
tmux send-keys -t zsh-completion-test "C-u"
sleep 0.2

# =============================================================================
# TEST 3: Client mode options (client --<TAB>)
# =============================================================================
echo ""
echo "=== TEST 3: ascii-chat client --<TAB> ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat client --" Tab
sleep 0.2

# Navigate completion menu
tmux send-keys -t zsh-completion-test "Down"
sleep 0.2

# Capture and save output
tmux capture-pane -t zsh-completion-test -p > /tmp/test3_output.txt

# Display what we got
echo "Captured output:"
cat /tmp/test3_output.txt | tail -20
echo ""

# Check for success indicators
if cat /tmp/test3_output.txt | rg -q "audio options"; then
  echo "🟢 TEST 3 PASSED: Client mode --audio option showing"
  TEST3_PASS=1
else
  echo "🔴 TEST 3 FAILED: Client mode --audio option NOT showing"
  TEST3_PASS=0
fi

# Cleanup
tmux send-keys -t zsh-completion-test "Escape"
sleep 0.2
tmux send-keys -t zsh-completion-test "C-u"
sleep 0.2

# =============================================================================
# TEST 4: Mode completion with partial input (disc)
# =============================================================================
echo ""
echo "=== TEST 4: ascii-chat disc<TAB> (checking for correction messages) ==="
tmux send-keys -t zsh-completion-test "./build/bin/ascii-chat disc" Tab
sleep 0.2

# Capture and save output (with last 100 lines like TEST 1)
tmux capture-pane -t zsh-completion-test -p -S -100 > /tmp/test4_output.txt

# Display what we got
echo "Captured output:"
cat /tmp/test4_output.txt | tail -20
echo ""

# Count correction messages and verify we have actual output
CORRECTIONS_COUNT=$(cat /tmp/test4_output.txt | rg -c "corrections \(errors: [0-9]\)" || echo 0)
CONTENT_LINES=$(cat /tmp/test4_output.txt | wc -l)
if [ "$CONTENT_LINES" -lt 5 ]; then
  echo "🔴 TEST 4 FAILED: No output captured (clear may have wiped screen)"
  TEST4_PASS=0
elif [ "$CORRECTIONS_COUNT" -eq 0 ]; then
  echo "🟢 TEST 4 PASSED: No correction messages found"
  TEST4_PASS=1
else
  echo "🔴 TEST 4 FAILED: Found $CORRECTIONS_COUNT correction message(s)"
  TEST4_PASS=0
fi

# Cleanup
tmux send-keys -t zsh-completion-test "Escape"
tmux send-keys -t zsh-completion-test "C-u"
echo ""
echo "Cleaning up..."
tmux kill-session -t zsh-completion-test 2>/dev/null || true

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "========== SUMMARY =========="
if [ "$TEST1_PASS" -eq 1 ] && [ "$TEST2_PASS" -eq 1 ] && [ "$TEST3_PASS" -eq 1 ] && [ "$TEST4_PASS" -eq 1 ]; then
  echo "🟢 ALL INTERACTIVE TESTS PASSED"
  exit 0
else
  echo "🔴 SOME TESTS FAILED"
  exit 1
fi
