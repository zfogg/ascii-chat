#!/usr/bin/env bash
# Test suite for scripts/version.sh

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="$PROJECT_ROOT/scripts/version.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

PASS=0
FAIL=0

chmod +x "$SCRIPT" 2>/dev/null || true

test_version() {
    local tag="$1"
    local args="$2"

    local tmpdir=$(mktemp -d)
    cd "$tmpdir"
    git init -q >/dev/null 2>&1
    git config user.email "test@example.com" 2>/dev/null
    git config user.name "Test" 2>/dev/null
    echo "x" > f 2>/dev/null
    git add f 2>/dev/null
    git commit -q -m "init" 2>/dev/null
    [[ -n "$tag" ]] && git tag "$tag" 2>/dev/null

    bash "$SCRIPT" $args 2>&1

    cd - >/dev/null
    rm -rf "$tmpdir"
}

check() {
    local test_name="$1"
    local expected="$2"
    local tag="$3"
    local args="$4"

    local result=$(test_version "$tag" "$args")

    if [[ "$result" == "$expected" ]]; then
        echo -e "  ${GREEN}✓${NC} $test_name: $result"
        ((PASS++))
    else
        echo -e "  ${RED}✗${NC} $test_name: expected '$expected', got '$result'"
        ((FAIL++))
    fi
}

echo "===== Testing scripts/version.sh ====="
echo ""

echo "Test 1: No tags (default 0.0.0)"
check "full" "0.0.0" "" ""
check "--major" "0" "" "--major"
check "--minor" "0" "" "--minor"
check "--patch" "0" "" "--patch"
check "--next" "0.0.1" "" "--next"
check "--next-major" "1.0.0" "" "--next-major"
check "--next-minor" "0.1.0" "" "--next-minor"
echo ""

echo "Test 2: v0.3.57"
check "full" "0.3.57" "v0.3.57" ""
check "--major (single)" "0" "v0.3.57" "--major"
check "--minor (single)" "3" "v0.3.57" "--minor"
check "--patch (single)" "57" "v0.3.57" "--patch"
check "--next (patch)" "0.3.58" "v0.3.57" "--next"
check "--next --major" "1.0.0" "v0.3.57" "--next --major"
check "--next --minor" "0.4.0" "v0.3.57" "--next --minor"
check "--next --patch" "0.3.58" "v0.3.57" "--next --patch"
check "--next-major" "1.0.0" "v0.3.57" "--next-major"
check "--next-minor" "0.4.0" "v0.3.57" "--next-minor"
check "--next-patch" "0.3.58" "v0.3.57" "--next-patch"
echo ""

echo "Test 3: v1.0.0"
check "full" "1.0.0" "v1.0.0" ""
check "--major" "1" "v1.0.0" "--major"
check "--minor" "0" "v1.0.0" "--minor"
check "--patch" "0" "v1.0.0" "--patch"
check "--next" "1.0.1" "v1.0.0" "--next"
echo ""

echo "Test 4: v99.200.5000 (large numbers)"
check "full" "99.200.5000" "v99.200.5000" ""
check "--major" "99" "v99.200.5000" "--major"
check "--minor" "200" "v99.200.5000" "--minor"
check "--patch" "5000" "v99.200.5000" "--patch"
check "--next" "99.200.5001" "v99.200.5000" "--next"
check "--next-major" "100.0.0" "v99.200.5000" "--next-major"
check "--next-minor" "99.201.0" "v99.200.5000" "--next-minor"
echo ""

echo "Test 5: v1.2.3-rc1 (pre-release suffix)"
check "full (stripped)" "1.2.3" "v1.2.3-rc1" ""
check "--major" "1" "v1.2.3-rc1" "--major"
check "--patch" "3" "v1.2.3-rc1" "--patch"
check "--next" "1.2.4" "v1.2.3-rc1" "--next"
echo ""

echo "Test 6: v10.20.30"
check "full" "10.20.30" "v10.20.30" ""
check "--major" "10" "v10.20.30" "--major"
check "--minor" "20" "v10.20.30" "--minor"
check "--patch" "30" "v10.20.30" "--patch"
check "--next" "10.20.31" "v10.20.30" "--next"
check "--next-minor" "10.21.0" "v10.20.30" "--next-minor"
check "--next-major" "11.0.0" "v10.20.30" "--next-major"
echo ""

echo "Test 7: Combinations (override rules)"
check "--major --next (next major wins)" "1.0.0" "v0.3.57" "--major --next"
check "--minor --next (next minor wins)" "0.4.0" "v0.3.57" "--minor --next"
check "--patch --next (next patch wins)" "0.3.58" "v0.3.57" "--patch --next"
check "--next-major --major (override 1)" "1.0.0" "v0.3.57" "--next-major --major"
echo ""

echo "Test 8: Help output"
result=$(test_version "" "--help" 2>&1)
if [[ "$result" =~ "Usage:" ]]; then
    echo -e "  ${GREEN}✓${NC} --help"
    ((PASS++))
else
    echo -e "  ${RED}✗${NC} --help"
    ((FAIL++))
fi

result=$(test_version "" "-h" 2>&1)
if [[ "$result" =~ "Usage:" ]]; then
    echo -e "  ${GREEN}✓${NC} -h"
    ((PASS++))
else
    echo -e "  ${RED}✗${NC} -h"
    ((FAIL++))
fi
echo ""

echo "Test 8: Error handling"
tmpdir=$(mktemp -d)
cd "$tmpdir"
git init -q >/dev/null 2>&1
git config user.email "test@example.com" 2>/dev/null
git config user.name "Test" 2>/dev/null
echo "x" > f 2>/dev/null
git add f 2>/dev/null
git commit -q -m "init" 2>/dev/null

if bash "$SCRIPT" --unknown >/dev/null 2>&1; then
    echo -e "  ${RED}✗${NC} --unknown should fail"
    ((FAIL++))
else
    echo -e "  ${GREEN}✓${NC} --unknown exits with error"
    ((PASS++))
fi

if bash "$SCRIPT" --bad >/dev/null 2>&1; then
    echo -e "  ${RED}✗${NC} --bad should fail"
    ((FAIL++))
else
    echo -e "  ${GREEN}✓${NC} --bad exits with error"
    ((PASS++))
fi

cd - >/dev/null
rm -rf "$tmpdir"
echo ""

echo "===== Summary ====="
echo -e "Passed: ${GREEN}$PASS${NC}"
echo -e "Failed: ${RED}$FAIL${NC}"
echo ""

if [[ $FAIL -gt 0 ]]; then
    exit 1
else
    echo -e "${GREEN}✅ All tests passed!${NC}"
    exit 0
fi
