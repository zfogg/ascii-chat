# Makefile Build Reference

## Quick Commands

```bash
make                    # Fast iteration build (default)
make production         # Production release build
make development        # Debug build with sanitizers
make release VERSION=v1.0.0  # Create release tarball
make clean              # Clean all build artifacts
```

## Build Targets

### `make` or `make fast`
**Default target for day-to-day development**

- **Output**: `build-fast/bin/`
- **Config**: glibc + clang, Dev mode (no sanitizers)
- **Size**: ~1MB binaries
- **Features**: Debug symbols, fast compile/run
- **Use when**: Quick iteration, testing features

### `make development`
**Debug build with full error detection**

- **Output**: `build-dev/bin/`
- **Config**: glibc + clang, Debug mode + ASan/UBSan
- **Size**: ~3-5MB binaries
- **Features**: DEBUG_MEMORY, sanitizers, full backtraces
- **Use when**: Debugging crashes, finding memory leaks

### `make production`
**Release build for distribution**

- **Output**: `build-production/bin/`
- **Config**: musl + mimalloc, Release mode, static
- **Size**: ~900KB binaries
- **Features**: Optimized, portable, no dependencies
- **Use when**: Creating releases, performance testing

### `make release VERSION=v1.0.0`
**Package release artifacts**

- Builds production binaries
- Creates tarball with docs
- Strips debug symbols
- **Output**: `release-v1.0.0.tar.gz`

## Binary Size Comparison

Your current setup (both Dev mode):
```
build/bin (musl):        936K server, 830K client
build_clang/bin (glibc): 1.1M server, 1.0M client
```

Expected sizes:
```
Production (musl+static):   ~900KB (optimized, stripped)
Development (glibc+asan):   ~3-5MB (debug+sanitizers)
Fast (glibc+debug):         ~1MB (debug symbols only)
```

## Build Directory Structure

```
ascii-chat/
├── build-production/   # make production
├── build-dev/          # make development
├── build-fast/         # make fast (default)
├── build/              # Legacy (BUILD_TYPE=...)
├── build_clang/        # Your custom builds
└── release-*/          # make release output
```

## Workflow Examples

### Daily Development
```bash
make                    # Build fast iteration
./build-fast/bin/ascii-chat server
# Make changes...
make                    # Rebuild quickly
```

### Debugging a Crash
```bash
make development        # Full sanitizers
./build-dev/bin/ascii-chat server
# Sanitizers catch the bug with full stack trace
```

### Creating a Release
```bash
make production         # Test production build
make release VERSION=v1.2.3
# Upload release-v1.2.3.tar.gz to GitHub
```

### Multiple Builds in Parallel
All builds use separate directories, so you can have all three simultaneously:
```bash
make production         # Build in build-production/
make development        # Build in build-dev/
make fast              # Build in build-fast/

# Test production performance
./build-production/bin/ascii-chat server

# Debug with sanitizers
./build-dev/bin/ascii-chat server

# Quick iteration
./build-fast/bin/ascii-chat server
```

## Legacy Compatibility

Old targets still work for backwards compatibility:
```bash
make all               # Same as: make BUILD_TYPE=Debug
make server            # Build server only
make client            # Build client only
make BUILD_TYPE=Release all
```

## Advanced Usage

### Override Parallelism
```bash
make NPROC=8 production    # Use 8 cores
```

### Custom Build Type (legacy)
```bash
make BUILD_TYPE=Coverage all
```

### Environment Variables
The new targets set CC/CXX automatically:
- `production`: Uses musl-gcc
- `development`: Uses clang
- `fast`: Uses clang

## Integration with CI/CD

GitHub Actions example:
```yaml
- name: Production Build
  run: make production

- name: Test with Sanitizers
  run: |
    make development
    ./build-dev/bin/ascii-chat server --help

- name: Create Release
  run: make release VERSION=${{ github.ref_name }}
```
