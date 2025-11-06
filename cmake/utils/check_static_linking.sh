#!/usr/bin/env bash
# Check if a binary is statically linked (Release builds only)
# Usage: check_static_linking.sh <binary_path> <platform>
#   platform: linux|windows|macos

BINARY="$1"
PLATFORM="$2"

# Colors
GREEN='\033[32m'
YELLOW='\033[33m'
RESET='\033[0m'

case "$PLATFORM" in
    linux)
        if ldd "$BINARY" 2>&1 | grep -q 'not a dynamic executable\|statically linked'; then
            echo -e "${GREEN}✓ Binary is statically linked${RESET}"
            exit 0
        else
            echo -e "${YELLOW}WARNING: Release build is NOT statically linked!${RESET}"
            ldd "$BINARY"
            exit 1
        fi
        ;;
    windows)
        # Check that only system DLLs are linked (case-insensitive)
        NON_SYSTEM=$(ldd "$BINARY" | grep -viE '(ntdll\.dll|kernel32\.dll|kernelbase\.dll|ws2_32\.dll|ucrtbase\.dll|advapi32\.dll|bcrypt\.dll|user32\.dll|msvcrt\.dll|secur32\.dll|crypt32\.dll|rpcrt4\.dll|ole32\.dll|oleaut32\.dll|shell32\.dll|shlwapi\.dll|shcore\.dll|winmm\.dll|mfplat\.dll|mf\.dll|mfreadwrite\.dll|mfuuid\.dll|dbghelp\.dll|vcruntime.*\.dll|msvcp.*\.dll|api-ms-win.*\.dll|/c/WINDOWS)' | grep -i '\.dll')
        if [ -n "$NON_SYSTEM" ]; then
            echo -e "${YELLOW}WARNING: Release build links against non-system DLLs!${RESET}"
            echo "$NON_SYSTEM"
            exit 1
        else
            echo -e "${GREEN}✓ Binary only links system DLLs${RESET}"
            exit 0
        fi
        ;;
    macos)
        NON_SYSTEM=$(otool -L "$BINARY" | grep -v '/usr/lib\|/System/Library\|@rpath' | tail -n +2)
        if [ -n "$NON_SYSTEM" ]; then
            echo -e "${YELLOW}WARNING: Release build links against non-system libraries!${RESET}"
            echo "$NON_SYSTEM"
            exit 1
        else
            echo -e "${GREEN}✓ Binary only links system libraries${RESET}"
            exit 0
        fi
        ;;
    *)
        echo "Unknown platform: $PLATFORM"
        exit 1
        ;;
esac
