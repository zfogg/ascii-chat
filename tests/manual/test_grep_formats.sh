#!/bin/bash
# Test script to demonstrate --grep works with both /regex/flags and plain regex formats

set -e

echo "=== Testing --grep with both formats ==="
echo ""

# Test 1: Plain regex format (no slashes)
echo "Test 1: Plain regex format - pattern: 'test'"
cat << 'EOF' | ./build/bin/test_grep_demo
This is a test message
This is another message
test at the start
EOF

echo ""

# Test 2: Slash format with case-insensitive flag
echo "Test 2: Slash format with flag - pattern: '/test/i'"
cat << 'EOF' | ./build/bin/test_grep_demo
This is a TEST message
This is another message
test in lowercase
EOF

echo ""

# Test 3: Plain regex with alternation
echo "Test 3: Plain regex alternation - pattern: 'error|warn'"
cat << 'EOF' | ./build/bin/test_grep_demo
error: something failed
warn: check this
info: normal operation
EOF

echo ""

# Test 4: Slash format with context lines
echo "Test 4: Slash format with context - pattern: '/ERROR/A2'"
cat << 'EOF' | ./build/bin/test_grep_demo
Before line 1
Before line 2
ERROR found here
After line 1
After line 2
After line 3
EOF

echo ""

# Test 5: Plain regex with regex metacharacters
echo "Test 5: Plain regex with digits - pattern: '\\d+'"
cat << 'EOF' | ./build/bin/test_grep_demo
Port 8080 opened
No numbers here
Connection on 192.168.1.1
EOF

echo ""

# Test 6: Slash format with fixed string
echo "Test 6: Fixed string (literal) - pattern: '/(test.*)/F'"
cat << 'EOF' | ./build/bin/test_grep_demo
Looking for (test.*) pattern
This test.* should match literally
This test followed by anything should NOT match
EOF

echo ""
echo "=== All format tests completed successfully ==="