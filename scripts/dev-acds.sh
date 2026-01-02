#!/bin/bash
# Development helper for ACDS - auto-rebuild and restart on file changes
# Usage: ./scripts/dev-acds.sh [acds-args...]
#
# Examples:
#   ./scripts/dev-acds.sh
#   ./scripts/dev-acds.sh --port 27226
#   ./scripts/dev-acds.sh --key /tmp/test_key --database /tmp/test.db

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}🔨 Building ACDS (initial build)...${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# Initial build
if ! cmake --build build --target acds; then
    echo -e "${RED}❌ Initial build failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Build successful${NC}"
echo ""
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GREEN}👀 Watching for changes...${NC}"
echo -e "${YELLOW}   Monitored: src/acds/, lib/options/, lib/network/, lib/util/path.c${NC}"
echo -e "${YELLOW}   Args to ACDS: $*${NC}"
echo -e "${YELLOW}   Press Ctrl+C to stop${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Create inline rebuild script
cat > /tmp/acds-rebuild.sh << 'REBUILD_SCRIPT'
#!/bin/bash
cd /home/zfogg/src/github.com/zfogg/ascii-chat
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔨 Rebuilding ACDS..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if cmake --build build --target acds 2>&1 | tail -5; then
    echo "✅ Build successful"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "🚀 Restarting ACDS..."
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    exec ./build/bin/acds "$@"
else
    echo "❌ Build failed - waiting for fixes..."
    sleep 999999
fi
REBUILD_SCRIPT

chmod +x /tmp/acds-rebuild.sh

# Watch for changes and rebuild+restart
# -r: restart command on file change
# -c: clear screen before running
find src/acds lib/options lib/network lib/util/path.c -name '*.c' -o -name '*.h' 2>/dev/null | \
    entr -rc /tmp/acds-rebuild.sh "$@"
