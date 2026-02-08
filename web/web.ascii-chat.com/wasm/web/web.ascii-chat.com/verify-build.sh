#!/bin/bash
# Verification script for WebAssembly client build

set -e

echo "==================================="
echo "WebAssembly Client Build Verification"
echo "==================================="
echo ""

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Check if emscripten is available
if ! command -v emcc &> /dev/null; then
    echo -e "${RED}✗ Emscripten not found${NC}"
    echo "  Install Emscripten: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi
echo -e "${GREEN}✓ Emscripten found${NC}"
echo "  Version: $(emcc --version | head -n1)"
echo ""

# Build WASM modules
echo "Building WASM modules..."
bun run wasm:build

# Verify outputs
echo ""
echo "Verifying build outputs..."

if [ -f "src/wasm/dist/client.wasm" ]; then
    CLIENT_SIZE=$(du -h src/wasm/dist/client.wasm | cut -f1)
    echo -e "${GREEN}✓ client.wasm${NC} ($CLIENT_SIZE)"
else
    echo -e "${RED}✗ client.wasm not found${NC}"
    exit 1
fi

if [ -f "src/wasm/dist/client.js" ]; then
    echo -e "${GREEN}✓ client.js${NC}"
else
    echo -e "${RED}✗ client.js not found${NC}"
    exit 1
fi

if [ -f "src/wasm/dist/mirror.wasm" ]; then
    MIRROR_SIZE=$(du -h src/wasm/dist/mirror.wasm | cut -f1)
    echo -e "${GREEN}✓ mirror.wasm${NC} ($MIRROR_SIZE)"
else
    echo -e "${RED}✗ mirror.wasm not found${NC}"
    exit 1
fi

if [ -f "src/wasm/dist/mirror.js" ]; then
    echo -e "${GREEN}✓ mirror.js${NC}"
else
    echo -e "${RED}✗ mirror.js not found${NC}"
    exit 1
fi

echo ""
echo "==================================="
echo -e "${GREEN}Build verification complete!${NC}"
echo "==================================="
echo ""
echo "Next steps:"
echo "  1. Install dependencies: bun install"
echo "  2. Run unit tests: bun run test:unit"
echo "  3. Start dev server: bun run dev"
echo "  4. Visit demo page: http://localhost:5173/client"
echo ""
echo "For E2E tests:"
echo "  1. Start native server: ./build/bin/ascii-chat server --port 27224"
echo "  2. Run E2E tests: bun run test:e2e"
