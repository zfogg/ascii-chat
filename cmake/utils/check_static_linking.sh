#!/usr/bin/env bash
# Check if a binary is statically linked (Release builds only)
# Usage: check_static_linking.sh <binary_path> <platform>
#   platform: linux|windows|macos

BINARY="$1"
PLATFORM="$2"
AUDIT_TOOL="${3:-}"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary does not exist: $BINARY"
    exit 1
fi

# Colors
GREEN='\033[32m'
YELLOW='\033[33m'
RESET='\033[0m'

case "$PLATFORM" in
    linux)
        if [ -z "$AUDIT_TOOL" ] || [ ! -x "$AUDIT_TOOL" ]; then
            echo "ERROR: llvm-readelf is required for the Linux static-link audit"
            exit 1
        fi
        DYNAMIC_OUTPUT=$("$AUDIT_TOOL" --dynamic "$BINARY") || {
            echo "ERROR: llvm-readelf failed to inspect $BINARY"
            exit 1
        }
        NEEDED=$(printf '%s\n' "$DYNAMIC_OUTPUT" | grep '(NEEDED)' || true)
        if [ -n "$NEEDED" ]; then
            echo -e "${YELLOW}ERROR: Release executable has dynamic dependencies:${RESET}"
            echo "$NEEDED"
            exit 1
        fi
        echo -e "${GREEN}✓ Binary has no dynamic ELF dependencies${RESET}"
        exit 0
        ;;
    windows)
        if [ -z "$AUDIT_TOOL" ] || [ ! -x "$AUDIT_TOOL" ]; then
            echo "ERROR: llvm-readobj is required for the Windows static-link audit"
            exit 1
        fi
        IMPORT_OUTPUT=$("$AUDIT_TOOL" --coff-imports "$BINARY") || {
            echo "ERROR: llvm-readobj failed to inspect $BINARY"
            exit 1
        }
        IMPORTS=$(printf '%s\n' "$IMPORT_OUTPUT" | sed -n 's/^[[:space:]]*Name: //p')
        if [ -z "$IMPORTS" ]; then
            echo "ERROR: llvm-readobj reported no imports for $BINARY; refusing an empty audit"
            exit 1
        fi
        NON_SYSTEM=""
        while IFS= read -r DLL; do
            LOWER=$(printf '%s' "$DLL" | tr '[:upper:]' '[:lower:]')
            case "$LOWER" in
                advapi32.dll|bcrypt.dll|bcryptprimitives.dll|cfgmgr32.dll|combase.dll|comctl32.dll|comdlg32.dll|crypt32.dll|dbghelp.dll|dnsapi.dll|gdi32.dll|gdi32full.dll|imm32.dll|iphlpapi.dll|kernel32.dll|kernelbase.dll|mf.dll|mfplat.dll|mfreadwrite.dll|mfuuid.dll|msvcrt.dll|mswsock.dll|normaliz.dll|ntdll.dll|ole32.dll|oleacc.dll|oleaut32.dll|psapi.dll|rpcrt4.dll|secur32.dll|setupapi.dll|shell32.dll|shcore.dll|shlwapi.dll|user32.dll|version.dll|winhttp.dll|winmm.dll|wintrust.dll|wldap32.dll|ws2_32.dll|wsock32.dll|ucrtbase.dll|api-ms-win-*.dll|ext-ms-win-*.dll)
                    ;;
                *) NON_SYSTEM="${NON_SYSTEM}${DLL}\n" ;;
            esac
        done <<EOF
$IMPORTS
EOF
        if [ -n "$NON_SYSTEM" ]; then
            printf "${YELLOW}ERROR: Release executable imports non-system DLLs:\n${RESET}%b" "$NON_SYSTEM"
            exit 1
        else
            echo -e "${GREEN}✓ Binary imports only Windows system DLLs${RESET}"
            exit 0
        fi
        ;;
    macos)
        # macOS executables cannot be fully static; allow only Apple system libraries.
        OTOOL_OUTPUT=$(otool -L "$BINARY") || {
            echo "ERROR: otool failed to inspect $BINARY"
            exit 1
        }
        NON_SYSTEM=$(printf '%s\n' "$OTOOL_OUTPUT" | grep -vE '^[[:space:]]*(/usr/lib/|/System/Library/|/Library/Apple/System/Library/)' | tail -n +2)
        if [ -n "$NON_SYSTEM" ]; then
            echo -e "${YELLOW}ERROR: Release binary links against non-system libraries!${RESET}"
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
