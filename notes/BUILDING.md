# Building ascii-chat

Complete guide to building ascii-chat on Linux, macOS, and Windows.

## Quick Start

```bash
# Standard Debug build (recommended for development)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run the server and client
./build/bin/ascii-chat server
./build/bin/ascii-chat client --test-pattern
```

## Build Types

ascii-chat supports multiple build configurations via `CMAKE_BUILD_TYPE`:

| Build Type | Optimization | Debug Symbols | Sanitizers | Use Case |
|------------|-------------|---------------|------------|----------|
| **Debug** | `-O0` | Yes | ASan, UBSan | Finding bugs, memory issues |
| **Dev** | `-O0` | Yes | No | Fast compilation, daily development |
| **Release** | `-O3` | No | No | Production, performance testing |

**Coverage Option**: Use `-DASCIICHAT_ENABLE_COVERAGE=ON` with any build type for code coverage.

### Debug Build (Default - Recommended)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

**Features:**
- AddressSanitizer catches memory errors (use-after-free, buffer overflows)
- UndefinedBehaviorSanitizer catches undefined behavior
- Full debug symbols for backtraces
- Unoptimized code for easier debugging

**When to use:** Finding crashes, memory leaks, undefined behavior, or investigating any bug.

### Dev Build (Fast Iteration)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Dev
cmake --build build
```

**Features:**
- Debug symbols but NO sanitizers
- Fast compilation and runtime
- Good for quick edit-compile-test cycles

**When to use:** Daily development when sanitizer overhead is too slow.

### Release Build (Production)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Features:**
- Full optimizations (`-O3`)
- No debug symbols (smaller binaries)
- No sanitizers (maximum performance)

**When to use:** Production deployments, performance testing, creating releases.

## Platform-Specific Builds

### Linux

```bash
# Standard build (dynamic glibc)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Static musl build (portable across distros)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_MUSL=ON
cmake --build build
```

**musl builds:**
- Create fully static binaries
- No runtime dependencies
- Portable across Linux distributions
- **Linux only** (not compatible with macOS or Windows)

### macOS

```bash
# Standard build
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Note: musl is NOT supported on macOS (uses libSystem)
```

**Requirements:**
- Xcode Command Line Tools
- Homebrew (for dependencies)

### Windows

#### Using PowerShell Build Script (Recommended)

```powershell
# Default build (Clang, Debug)
./build.ps1

# Release build
./build.ps1 -Config Release

# Clean rebuild
./build.ps1 -Clean

# MinGW mode
./build.ps1 -MinGW

# With tests (requires MinGW + Criterion)
./build.ps1 -Test

# Verbose output
./build.ps1 -Verbose
```

The build script automatically:
- Detects best compiler (Clang > MSVC > GCC)
- Uses Ninja for faster builds
- Creates hard links in `bin/` directory
- Links `compile_commands.json` for IDEs
- Kills conflicting processes

#### Manual CMake (Windows)

```powershell
# With Clang (recommended)
cmake -B build -G "Ninja" -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# With Visual Studio
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug

# With MinGW
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

**Note:** Criterion tests typically only work with MinGW on Windows.

## Memory Allocators

### mimalloc (Default, All Platforms)

mimalloc is a high-performance allocator from Microsoft, enabled by default.

```bash
# Already enabled by default
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Disable if needed
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DUSE_MIMALLOC=OFF
cmake --build build
```

**Installation:**
- **Linux (Arch):** `sudo pacman -S mimalloc`
- **Linux (Ubuntu):** `sudo apt install libmimalloc-dev`
- **macOS:** `brew install mimalloc`
- **Windows:** Auto-downloaded during CMake config

**Benefits:**
- Drop-in replacement for malloc/free
- Significant performance improvements
- No code changes required
- Works with `SAFE_MALLOC()` macros

### System malloc

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DUSE_MIMALLOC=OFF
cmake --build build
```

Use when debugging allocator-specific issues or if mimalloc is unavailable.

## Common Build Options

### Enable Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build --target tests

# Run tests
ctest --test-dir build --output-on-failure --parallel 0
```

**Note:** Tests require the Criterion framework (typically Unix only, or MinGW on Windows).

### Disable Audio

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_AUDIO=OFF
cmake --build build
```

### Enable Coverage

Coverage is an option that can be combined with any build type:

```bash
# Debug build with coverage (recommended)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Or use the coverage preset
cmake --preset coverage
cmake --build build
```

## Recommended Workflows

### Daily Development

```bash
# Fast iteration mode
cmake -B build -DCMAKE_BUILD_TYPE=Dev
cmake --build build

# Format code after changes
cmake --build build --target format
```

### Debugging Issues

```bash
# Full sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with verbose logging
LOG_LEVEL=DEBUG ./build/bin/ascii-chat server
```

### Performance Testing

```bash
# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_MIMALLOC=ON
cmake --build build

# Benchmark
time ./build/bin/ascii-chat server --benchmark
```

### Creating Releases (Linux)

```bash
# Static musl build for distribution
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_MUSL=ON -DUSE_MIMALLOC=ON
cmake --build build

# Binary is fully static and portable
ldd ./build/bin/ascii-chat server
# Output: "not a dynamic executable"
```

## Troubleshooting

### Build Fails with "Cannot find mimalloc"

Install mimalloc or disable it:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DUSE_MIMALLOC=OFF
cmake --build build
```

### Sanitizer Reports False Positives

Disable sanitizers using Dev mode:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Dev
cmake --build build
```

Or suppress specific checks:
```bash
ASAN_OPTIONS=detect_leaks=0 ./build/bin/ascii-chat server
```

### musl Build Fails (Linux)

Ensure musl-gcc is installed:
```bash
# Arch
sudo pacman -S musl

# Ubuntu/Debian
sudo apt install musl-tools musl-dev
```

### Windows Build Script Fails

Try manual CMake:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### ccache Not Working with musl

This is intentional. musl-gcc is a wrapper script incompatible with ccache. The build system automatically disables ccache when `USE_MUSL=ON`.

## Build Directories

Different build types can coexist:

```bash
# Development build
cmake -B build-dev -DCMAKE_BUILD_TYPE=Dev
cmake --build build-dev

# Debug build with sanitizers
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug

# Production build
cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DUSE_MUSL=ON
cmake --build build-release

# All executables are independent
./build-dev/bin/ascii-chat server
./build-debug/bin/ascii-chat server
./build-release/bin/ascii-chat server
```

## Clean Builds

```bash
# Remove build directory
rm -rf build

# Reconfigure and rebuild
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Platform Summary

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| **Compiler** | gcc, clang | clang | clang, MSVC, MinGW |
| **CMake** | ✅ Yes | ✅ Yes | ✅ Yes |
| **musl (static)** | ✅ Yes | ❌ No | ❌ No |
| **mimalloc** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Sanitizers** | ✅ Yes | ✅ Yes | ⚠️ Limited |
| **Tests (Criterion)** | ✅ Yes | ✅ Yes | ⚠️ MinGW only |
| **Build Script** | ❌ No | ❌ No | ✅ build.ps1 |

## Advanced Options

For advanced CMake options and custom configurations, see the main `CMakeLists.txt`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DUSE_MUSL=OFF \
    -DUSE_MIMALLOC=ON \
    -DBUILD_TESTS=ON \
    -DENABLE_AUDIO=ON

cmake --build build -j$(nproc)
```

## Summary

**Most developers should use:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

This enables sanitizers, debug symbols, and mimalloc by default - the optimal configuration for finding bugs quickly.
