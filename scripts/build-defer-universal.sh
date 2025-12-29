#!/bin/bash
# Build universal (arm64 + x86_64) defer tool binary for macOS
#
# Prerequisites:
#   - LLVM 22+ built for arm64 (default: /usr/local with llvm-config)
#   - LLVM 22+ built for x86_64 (default: ~/src/github.com/llvm/llvm-project/build-x86_64)
#
# Usage:
#   ./scripts/build-defer-universal.sh
#
# Environment variables:
#   LLVM_ARM64_CONFIG  - Path to llvm-config for arm64 (default: /usr/local/bin/llvm-config)
#   LLVM_X86_64_ROOT   - Path to x86_64 LLVM build (default: ~/src/github.com/llvm/llvm-project/build-x86_64)
#   LLVM_SOURCE        - Path to LLVM source (for include paths, default: ~/src/github.com/llvm/llvm-project)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Configuration
LLVM_ARM64_CONFIG="${LLVM_ARM64_CONFIG:-/usr/local/bin/llvm-config}"
LLVM_X86_64_ROOT="${LLVM_X86_64_ROOT:-$HOME/src/github.com/llvm/llvm-project/build-x86_64}"
LLVM_SOURCE="${LLVM_SOURCE:-$HOME/src/github.com/llvm/llvm-project}"

OUTPUT_DIR="$PROJECT_ROOT/.deps-cache/defer-tool-universal"
ARM64_DIR="$PROJECT_ROOT/.deps-cache/defer-tool-arm64"
X86_64_DIR="$PROJECT_ROOT/.deps-cache/defer-tool-x86_64"

TOOL_NAME="ascii-instr-defer"
TOOL_SOURCE_DIR="$PROJECT_ROOT/src/tooling/defer"

echo "=== Building Universal Defer Tool ==="
echo "Project root:     $PROJECT_ROOT"
echo "LLVM arm64 cfg:   $LLVM_ARM64_CONFIG"
echo "LLVM x86_64 root: $LLVM_X86_64_ROOT"
echo "LLVM source:      $LLVM_SOURCE"
echo ""

# Verify prerequisites
if [[ ! -x "$LLVM_ARM64_CONFIG" ]]; then
    echo "ERROR: arm64 llvm-config not found at $LLVM_ARM64_CONFIG"
    exit 1
fi

if [[ ! -f "$LLVM_X86_64_ROOT/lib/libLLVMSupport.a" ]]; then
    echo "ERROR: x86_64 LLVM not found at $LLVM_X86_64_ROOT"
    exit 1
fi

# Get LLVM version
LLVM_VERSION=$("$LLVM_ARM64_CONFIG" --version | cut -d. -f1)
echo "LLVM version: $LLVM_VERSION"

# Create/clean directories
rm -rf "$ARM64_DIR" "$X86_64_DIR" "$OUTPUT_DIR"
mkdir -p "$ARM64_DIR" "$X86_64_DIR" "$OUTPUT_DIR"

# Build arm64 using cmake
echo ""
echo "=== Building arm64 ==="
cd "$ARM64_DIR"

cmake "$TOOL_SOURCE_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/local/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DLLVM_CONFIG_EXECUTABLE="$LLVM_ARM64_CONFIG" \
    -DOUTPUT_DIR="$ARM64_DIR"

cmake --build . --target "$TOOL_NAME"

echo "arm64 build complete:"
file "$ARM64_DIR/$TOOL_NAME"

# Build x86_64 using cmake
echo ""
echo "=== Building x86_64 ==="
cd "$X86_64_DIR"

# For x86_64, we need to point to the x86_64 LLVM build
# Create a wrapper script that acts like llvm-config for the x86_64 build
X86_LLVM_CONFIG="$X86_64_DIR/llvm-config-x86_64.sh"
cat > "$X86_LLVM_CONFIG" << EOF
#!/bin/bash
# Wrapper llvm-config for x86_64 LLVM build
LLVM_ROOT="$LLVM_X86_64_ROOT"
LLVM_SOURCE="$LLVM_SOURCE"

case "\$1" in
    --version)
        echo "$LLVM_VERSION.0.0"
        ;;
    --prefix)
        echo "\$LLVM_ROOT"
        ;;
    --includedir)
        # For source builds, include both LLVM and Clang headers
        # Use semicolons as cmake list separator
        echo "\$LLVM_SOURCE/llvm/include;\$LLVM_ROOT/include;\$LLVM_SOURCE/clang/include;\$LLVM_ROOT/tools/clang/include"
        ;;
    --libdir)
        echo "\$LLVM_ROOT/lib"
        ;;
    --cxxflags)
        echo "-I\$LLVM_SOURCE/llvm/include -I\$LLVM_ROOT/include -I\$LLVM_SOURCE/clang/include -I\$LLVM_ROOT/tools/clang/include -std=c++17 -fno-exceptions -funwind-tables -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS"
        ;;
    --ldflags)
        echo "-L\$LLVM_ROOT/lib"
        ;;
    --system-libs)
        echo "-lz -lcurses -lm"
        ;;
    --libs)
        # Output libraries as -l flags (like real llvm-config)
        first=1
        for lib in \$LLVM_ROOT/lib/libLLVM*.a; do
            if [[ -f "\$lib" ]]; then
                # Extract library name: libLLVMFoo.a -> LLVMFoo
                basename="\$(basename "\$lib" .a)"
                libname="\${basename#lib}"
                if [[ \$first -eq 1 ]]; then
                    printf "%s" "-l\$libname"
                    first=0
                else
                    printf " %s" "-l\$libname"
                fi
            fi
        done
        echo ""
        ;;
    *)
        echo "Unknown option: \$1" >&2
        exit 1
        ;;
esac
EOF
chmod +x "$X86_LLVM_CONFIG"

# For x86_64 cross-compilation, we need to:
# 1. Prevent cmake from finding arm64 Homebrew libraries
# 2. Use SDK libraries which support both architectures
# 3. Point to x86_64 LLVM libraries for linking
MACOS_SDK="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"

cmake "$TOOL_SOURCE_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_C_COMPILER=/usr/bin/clang \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_OSX_SYSROOT="$MACOS_SDK" \
    -DCMAKE_FIND_ROOT_PATH="$MACOS_SDK;$LLVM_X86_64_ROOT" \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_IGNORE_PREFIX_PATH="/opt/homebrew;/usr/local" \
    -DZLIB_ROOT="$MACOS_SDK/usr" \
    -DZLIB_LIBRARY="$MACOS_SDK/usr/lib/libz.tbd" \
    -DZLIB_INCLUDE_DIR="$MACOS_SDK/usr/include" \
    -DLLVM_CONFIG_EXECUTABLE="$X86_LLVM_CONFIG" \
    -DLLVM_X86_64_BUILD_ROOT="$LLVM_X86_64_ROOT" \
    -DLLVM_SOURCE_ROOT="$LLVM_SOURCE" \
    -DOUTPUT_DIR="$X86_64_DIR"

cmake --build . --target "$TOOL_NAME"

echo "x86_64 build complete:"
file "$X86_64_DIR/$TOOL_NAME"

# Create universal binary
echo ""
echo "=== Creating Universal Binary ==="
cd "$PROJECT_ROOT"

lipo -create \
    "$ARM64_DIR/$TOOL_NAME" \
    "$X86_64_DIR/$TOOL_NAME" \
    -output "$OUTPUT_DIR/$TOOL_NAME"

echo "Universal binary created:"
file "$OUTPUT_DIR/$TOOL_NAME"
lipo -info "$OUTPUT_DIR/$TOOL_NAME"
ls -lh "$OUTPUT_DIR/$TOOL_NAME"

echo ""
echo "=== Done ==="
echo "To upload to GitHub release:"
echo "  gh release upload build-tools $OUTPUT_DIR/$TOOL_NAME --clobber"
