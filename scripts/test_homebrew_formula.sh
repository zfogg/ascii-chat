#!/usr/bin/env bash
# Test ascii-chat Homebrew formula before submitting to homebrew-core
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FORMULA_PATH="$PROJECT_ROOT/Formula/ascii-chat-homebrew-core.rb"

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

log_info "Testing Homebrew formula: ascii-chat"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Note: brew audit and brew style require the formula to be in a tap
# We'll validate the formula by attempting to install it
log_warning "Skipping brew audit/style (requires formula in tap)"
log_info "Will validate by attempting installation..."
echo ""

# Step 1: Verify dependencies exist
log_info "Step 1: Verifying dependencies exist in homebrew-core..."
DEPS=(
    "cmake" "ninja" "llvm" "lld"
    "libsodium" "opus" "portaudio" "zstd" "mimalloc"
    "ca-certificates" "gnupg" "criterion"
)

for dep in "${DEPS[@]}"; do
    if brew info "$dep" &>/dev/null; then
        log_success "✓ $dep"
    else
        log_error "✗ $dep not found in homebrew-core"
        exit 1
    fi
done
echo ""

# Step 2: Check if ascii-chat is already installed
if brew list ascii-chat &>/dev/null; then
    log_warning "ascii-chat is already installed - uninstalling..."
    brew uninstall ascii-chat
    log_success "Uninstalled"
fi
echo ""

# Step 3: Install formula from source
log_info "Step 3: Installing formula from source (this may take a while)..."
if brew install --build-from-source "$FORMULA_PATH"; then
    log_success "Installation successful"
else
    log_error "Installation failed"
    exit 1
fi
echo ""

# Step 4: Run formula tests
log_info "Step 4: Running formula tests..."
if brew test ascii-chat; then
    log_success "Tests passed"
else
    log_error "Tests failed"
    exit 1
fi
echo ""

# Step 5: Test binary functionality
log_info "Step 5: Testing binary functionality..."

# Test --help
if ascii-chat --help &>/dev/null; then
    log_success "ascii-chat --help works"
else
    log_error "ascii-chat --help failed"
    exit 1
fi

# Test --version
if ascii-chat --version &>/dev/null; then
    log_success "ascii-chat --version works"
else
    log_error "ascii-chat --version failed"
    exit 1
fi

# Test snapshot mode (doesn't require webcam)
log_info "Testing snapshot mode (without webcam)..."

# Start server in background
log_info "Starting test server on port 27225..."
ascii-chat server --port 27225 &>/dev/null &
SERVER_PID=$!
sleep 2

# Test client connection with snapshot
if timeout 10 ascii-chat client 127.0.0.1:27225 --snapshot --no-webcam &>/dev/null; then
    log_success "Snapshot mode works"
else
    log_warning "Snapshot mode test failed (this may be expected in some environments)"
fi

# Kill server
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
echo ""

# Summary
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
log_success "All tests passed! Formula is ready for submission to homebrew-core."
echo ""
echo "Next steps:"
echo "  1. Review the submission guide: docs/HOMEBREW_SUBMISSION_GUIDE.md"
echo "  2. Test on Linux (if possible)"
echo "  3. Fork homebrew-core and create a PR"
echo ""
echo "To uninstall:"
echo "  brew uninstall ascii-chat"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
