# Building Tooling Without Application Dependencies

**Problem:** When you change `options.c`, `network.c`, or any application code, the tooling runtime (`ascii-debug-runtime`) rebuilds because it links against `ascii-chat-util`, `ascii-chat-core`, etc.

**Solution:** Use a pre-built or installed ascii-chat library for tooling, breaking the dependency on application code.

## Three Build Modes

### Mode 1: Development (Default)
**Behavior:** Tooling runtime builds from source and links against application libraries.
**When to use:** Quick builds, don't care about tooling rebuilds
**Rebuild trigger:** Any change to source code in `lib/`

```bash
cmake -B build
cmake --build build
```

✅ **Pro:** Simple, works out of the box
❌ **Con:** Tooling rebuilds when you touch `options.c`

### Mode 2: Installed Library
**Behavior:** Uses system-installed ascii-chat library
**When to use:** Production builds, CI/CD, or after installing from package manager
**Rebuild trigger:** Only changes to tooling source code (`src/tooling/`, `lib/tooling/`)

```bash
# Install ascii-chat first (from package or make install)
sudo make install  # or: pacman -S ascii-chat, apt install ascii-chat, etc.

# Then build with installed library
cmake -B build -DASCII_TOOLING_USE_INSTALLED_LIBS=ON
cmake --build build
```

✅ **Pro:** Tooling never rebuilds from app changes
✅ **Pro:** Uses stable, released version
❌ **Con:** Requires installing ascii-chat first

### Mode 3: Bootstrap with Custom Library (Recommended for Development)
**Behavior:** Build library once, reuse for tooling
**When to use:** Development workflow, avoids constant rebuilds
**Rebuild trigger:** Only tooling source changes

```bash
# Step 1: Build library WITHOUT instrumentation
cmake -B build
cmake --build build

# Step 2: Copy library to safe location (avoids circular dependency)
# Windows:
cp build/lib/asciichat.lib build/asciichat_for_tooling.lib

# Unix/macOS:
cp build/lib/libasciichat.a build/libasciichat_for_tooling.a

# Step 3: Reconfigure with instrumentation + library path
# Windows:
cmake -B build \
  -DASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION=ON \
  -DASCII_TOOLING_LIBRARY_PATH="build/asciichat_for_tooling.lib"

# Unix/macOS:
cmake -B build \
  -DASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION=ON \
  -DASCII_TOOLING_LIBRARY_PATH="build/libasciichat_for_tooling.a"

# Step 4: Build only tooling targets (not the whole project)
cmake --build build --target ascii-source-print-tool
```

✅ **Pro:** Tooling never rebuilds from app changes
✅ **Pro:** No install required
✅ **Pro:** Clean separation
❌ **Con:** Two-step build process

## Recommended Workflows

### For Active Development
Use **Mode 3** (bootstrap) to avoid constant tooling rebuilds:

```bash
# Initial setup
cmake -B build
cmake --build build --target ascii-chat-static

# Windows:
cmake -B build -DASCII_TOOLING_LIBRARY_PATH="build/lib/asciichat.lib"

# Unix/macOS:
cmake -B build -DASCII_TOOLING_LIBRARY_PATH="build/lib/libasciichat.a"

cmake --build build

# Now you can edit options.c, network.c, etc. without rebuilding tooling!
# Only rebuild library when you need updated tooling runtime:
cmake --build build --target ascii-chat-static
```

### For Source Print Instrumentation
When using source_print instrumentation, you need the tool AND runtime:

```bash
# Build library first
cmake -B build
cmake --build build --target ascii-chat-static

# Configure with instrumentation + library path
cmake -B build \
  -DASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION=ON \
  -DASCII_TOOLING_LIBRARY_PATH="build/lib/libasciichat.a"

# Build instrumented version
cmake --build build

# Now changes to app code don't rebuild ascii-source-print-tool!
```

### For Defer Instrumentation (Future)
Same workflow as source_print:

```bash
cmake -B build
cmake --build build --target ascii-chat-static

cmake -B build \
  -DASCII_BUILD_WITH_DEFER=ON \
  -DASCII_TOOLING_LIBRARY_PATH="build/lib/libasciichat.a"

cmake --build build
```

## How It Works

### Without Library Path (Mode 1)
```
ascii-debug-runtime
  ↓ links against
ascii-chat-util, ascii-chat-platform, ascii-chat-core
  ↓ contains
options.c, logging.c, network.c, ...

→ Change options.c → Rebuilds ascii-chat-util → Rebuilds ascii-debug-runtime → Rebuilds tooling
```

### With Library Path (Mode 3)
```
ascii-debug-runtime (INTERFACE library)
  ↓ links against
ascii-tooling-runtime-lib (IMPORTED)
  ↓ points to
build/lib/libasciichat.a (pre-built, frozen)

→ Change options.c → Does nothing! Library is already built.
→ Only rebuild library when you explicitly want to update it.
```

## Troubleshooting

### Library not found
```
CMake Error: ASCII_TOOLING_LIBRARY_PATH does not exist: build/lib/libasciichat.a
```

**Solution:** Build the library target first:
```bash
cmake --build build --target ascii-chat-static
```

### Wrong library path
Check the actual library location:

```bash
# Windows:
ls build/lib/*.lib

# Unix/macOS:
ls build/lib/*.a
```

Common locations:
- Windows MSVC: `build/lib/asciichat.lib`
- Windows MinGW: `build/lib/libasciichat.a`
- Unix/macOS: `build/lib/libasciichat.a`

### Tooling still rebuilds
Make sure you configured with the library path:

```bash
# Check current configuration
cmake -L build | grep TOOLING

# Should show:
# ASCII_TOOLING_LIBRARY_PATH:FILEPATH=build/lib/libasciichat.a
# ASCII_TOOLING_USE_INSTALLED_LIBS:BOOL=OFF
```

If not set, reconfigure:
```bash
cmake -B build -DASCII_TOOLING_LIBRARY_PATH="build/lib/libasciichat.a"
```

### Library is out of date
If you need to update the runtime (e.g., after changing platform code):

```bash
# Rebuild library
cmake --build build --target ascii-chat-static

# No need to reconfigure - just rebuild
cmake --build build
```

The tooling will automatically use the updated library.

## Performance Impact

**Mode 1 (Default):**
- Touch `options.c` → Rebuild 5+ targets (util, core, debug-runtime, tool, report)
- **~10-30 seconds** per edit

**Mode 3 (Bootstrap):**
- Touch `options.c` → Rebuild 0 targets
- **~0 seconds** per edit
- Only rebuild when you explicitly run `--target ascii-chat-static`

**Speedup:** **∞** (no rebuilds vs constant rebuilds)

## Integration with Build Scripts

### PowerShell (build.ps1)
```powershell
# Add parameter for bootstrap mode
param(
    [switch]$BootstrapTooling
)

if ($BootstrapTooling) {
    Write-Host "Building library for tooling..."
    cmake --build build --target ascii-chat-static

    $LibPath = "build/lib/asciichat.lib"
    cmake -B build -DASCII_TOOLING_LIBRARY_PATH="$LibPath"
}
```

Usage:
```powershell
./build.ps1 -BootstrapTooling
```

### Bash
```bash
#!/bin/bash
# build_with_tooling.sh

cmake -B build
cmake --build build --target ascii-chat-static

# Detect platform
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    LIB_PATH="build/lib/asciichat.lib"
else
    LIB_PATH="build/lib/libasciichat.a"
fi

cmake -B build -DASCII_TOOLING_LIBRARY_PATH="$LIB_PATH"
cmake --build build
```

## When to Use Each Mode

| Scenario | Mode | Why |
|----------|------|-----|
| Quick test build | 1 (default) | Simplest, no setup |
| Active development | 3 (bootstrap) | Avoids constant rebuilds |
| CI/CD pipeline | 2 (installed) | Uses stable release |
| Source instrumentation | 3 (bootstrap) | Best performance |
| Defer transformation | 3 (bootstrap) | Best performance |
| Package building | 2 (installed) | Reproducible builds |
| First-time user | 1 (default) | Just works |

## Summary

**Key takeaway:** Use `ASCII_TOOLING_LIBRARY_PATH` to point tooling at a pre-built library, breaking the dependency on application code. This prevents tooling from rebuilding every time you edit application files.

**Recommended for development:**
```bash
cmake -B build
cmake --build build --target ascii-chat-static
cmake -B build -DASCII_TOOLING_LIBRARY_PATH="build/lib/libasciichat.a"
cmake --build build
# Now edit options.c, network.c freely without tooling rebuilds!
```
