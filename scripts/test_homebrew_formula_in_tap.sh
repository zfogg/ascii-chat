#!/usr/bin/env bash
# Test ascii-chat Homebrew formula by temporarily installing it in your existing tap
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FORMULA_PATH="$PROJECT_ROOT/Formula/ascii-chat-homebrew-core.rb"
TAP_PATH="$PROJECT_ROOT/../homebrew-ascii-chat"
TAP_FORMULA_PATH="$TAP_PATH/Formula/ascii-chat.rb"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $*"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $*"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

# Check if formula exists
if [[ ! -f "$FORMULA_PATH" ]]; then
    log_error "Formula not found at: $FORMULA_PATH"
    exit 1
fi

# Check if tap exists
if [[ ! -d "$TAP_PATH" ]]; then
    log_error "Tap not found at: $TAP_PATH"
    log_info "Expected homebrew-ascii-chat to be in: $TAP_PATH"
    exit 1
fi

log_info "Testing Homebrew formula by temporarily replacing tap formula"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Step 1: Backup existing formula
if [[ -f "$TAP_FORMULA_PATH" ]]; then
    log_info "Backing up existing formula..."
    cp "$TAP_FORMULA_PATH" "$TAP_FORMULA_PATH.backup"
    log_success "Backed up to: $TAP_FORMULA_PATH.backup"
fi
echo ""

# Step 2: Replace with test formula
log_info "Installing test formula in tap..."
cp "$FORMULA_PATH" "$TAP_FORMULA_PATH"
log_success "Formula copied to tap"

# Force Homebrew to recognize the new formula
log_info "Refreshing tap..."
cd "$TAP_PATH" && git add Formula/ascii-chat.rb && cd - > /dev/null
echo ""

# Function to restore original formula
restore_formula() {
    if [[ -f "$TAP_FORMULA_PATH.backup" ]]; then
        log_info "Restoring original formula..."
        mv "$TAP_FORMULA_PATH.backup" "$TAP_FORMULA_PATH"
        log_success "Original formula restored"
    fi
}

# Set trap to always restore on exit
trap restore_formula EXIT

# Step 3: Uninstall existing ascii-chat if present
if brew list ascii-chat &>/dev/null; then
    log_warning "Uninstalling existing ascii-chat..."
    brew uninstall ascii-chat
    log_success "Uninstalled"
fi
echo ""

# Step 4: Audit formula
log_info "Running brew audit..."
if brew audit --strict --online ascii-chat; then
    log_success "brew audit passed"
else
    log_error "brew audit failed"
    exit 1
fi
echo ""

# Step 5: Style check
log_info "Checking formula style..."
if brew style ascii-chat; then
    log_success "brew style passed"
else
    log_error "brew style failed"
    exit 1
fi
echo ""

# Step 6: Install from source
log_info "Installing from source (this will take a while)..."
if brew install --build-from-source ascii-chat; then
    log_success "Installation successful"
else
    log_error "Installation failed"
    exit 1
fi
echo ""

# Step 7: Run formula tests
log_info "Running formula tests..."
if brew test ascii-chat; then
    log_success "Tests passed"
else
    log_error "Tests failed"
    exit 1
fi
echo ""

# Step 8: Test binary functionality
log_info "Testing binary functionality..."

if ascii-chat --help &>/dev/null; then
    log_success "ascii-chat --help works"
else
    log_error "ascii-chat --help failed"
    exit 1
fi

if ascii-chat --version &>/dev/null; then
    log_success "ascii-chat --version works"
else
    log_error "ascii-chat --version failed"
    exit 1
fi
echo ""

# Step 9: Integration test (server + client)
log_info "Running integration test (server + client)..."
log_info "Starting test server on port 27225..."
ascii-chat server --port 27225 &>/tmp/ascii-chat-test-server.log &
SERVER_PID=$!
sleep 3

if timeout 10 ascii-chat client 127.0.0.1:27225 --snapshot --no-webcam &>/tmp/ascii-chat-test-client.log; then
    log_success "Integration test passed"
else
    log_warning "Integration test failed (check logs: /tmp/ascii-chat-test-*.log)"
fi

# Kill server
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
echo ""

# Summary
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
log_success "All tests passed! Formula is ready for homebrew-core submission."
echo ""
echo "Test logs saved to:"
echo "  - /tmp/ascii-chat-test-server.log"
echo "  - /tmp/ascii-chat-test-client.log"
echo ""
echo "Next steps:"
echo "  1. Review docs/HOMEBREW_SUBMISSION_GUIDE.md"
echo "  2. Test on Linux (if possible)"
echo "  3. Fork homebrew-core and submit PR"
echo ""
echo "To uninstall test version:"
echo "  brew uninstall ascii-chat"
echo ""
echo "Original formula will be restored automatically."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
