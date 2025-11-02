# CMakeLists.txt Modularization Plan

## Current State
- **Total Lines**: 2,143 lines
- **Current Modules**: 13 existing `.cmake` files
- **Problem**: Main CMakeLists.txt is too long and hard to maintain

## Proposed Module Structure

### Priority 1: Extract Immediately (Large, Self-Contained Sections)

#### 1. **cmake/EarlySetup.cmake** (~82 lines)
**Lines 1-82** - Must be called BEFORE `project()`
- Dependency cache configuration (`.deps-cache`, `.deps-cache-musl`)
- vcpkg toolchain setup
- Ninja generator detection
- Build type defaults

**Functions to create:**
- `setup_dependency_cache()` - Configures FETCHCONTENT_BASE_DIR
- `configure_vcpkg_toolchain()` - Sets up vcpkg if available
- `configure_build_generator()` - Sets Ninja as default

**Prerequisites**: None (runs before project())

---

#### 2. **cmake/MuslPreProject.cmake** (~117 lines)
**Lines 86-202** - Must be called BEFORE `project()`
- musl libc detection and configuration
- Clang compiler selection
- LLVM toolchain configuration (ar, ranlib)
- musl static-PIE setup

**Functions to create:**
- `configure_musl_pre_project()` - Full musl setup before project()

**Prerequisites**:
- `CMAKE_HOST_SYSTEM_NAME`
- `CMAKE_BUILD_TYPE`
- `USE_MUSL` option (set by this module)

**Note**: Keep inline in main CMakeLists.txt OR ensure very clear documentation about execution order

---

#### 3. **cmake/ProjectConfiguration.cmake** (~50 lines)
**Lines 211-237, 242-256** - Called immediately after `project()`
- LLVM ranlib fix for Windows
- Include helper modules
- Post-project configuration calls

**Functions to create:**
- `configure_project_setup()` - Post-project() setup

**Prerequisites**: Must run after `project()`

---

#### 4. **cmake/CompilerDetection.cmake** (~137 lines)
**Lines 259-395**
- Environment variable handling (CC, CFLAGS, CPPFLAGS, LDFLAGS)
- Default compiler selection
- ccache detection
- compile_commands.json setup
- Windows Clang runtime flag clearing

**Functions to create:**
- `detect_and_configure_compiler()` - Full compiler setup
- `setup_compile_commands_json()` - Symlink/copy compile_commands.json

**Prerequisites**: Must run after `project()`

---

#### 5. **cmake/BuildConfiguration.cmake** (~115 lines)
**Lines 398-510**
- Terminal color definitions
- CPU core detection
- C standard selection (C23/C2X)
- Build type validation
- Output directories

**Functions to create:**
- `configure_terminal_colors()` - Sets up color variables
- `detect_cpu_cores()` - Detects parallel build level
- `configure_c_standard()` - C23/C2X detection and selection

**Prerequisites**: Runs after `project()`

---

#### 6. **cmake/OptionsConfiguration.cmake** (~75 lines)
**Lines 510-561**
- mimalloc option configuration (based on build type and musl)
- musl-specific options
- ccache configuration (via existing CCache.cmake)

**Functions to create:**
- `configure_mimalloc_option()` - Sets USE_MIMALLOC based on build type
- `configure_musl_options()` - musl-specific option setup

**Prerequisites**:
- `CMAKE_BUILD_TYPE`
- `USE_MUSL`
- Existing `CCache.cmake`

---

#### 7. **cmake/PlatformDetection.cmake** (~70 lines)
**Lines 569-638**
- Platform variable initialization
- Darwin/macOS detection (Apple Silicon, Rosetta)
- Linux detection
- Windows detection and architecture (ARM64, ARM, x64)
- GNU source definitions

**Functions to create:**
- `detect_platform()` - Sets all PLATFORM_* variables
- `detect_darwin_details()` - Apple Silicon/Rosetta detection

**Prerequisites**: Runs after `project()`

---

### Priority 2: Extract Large Source File Sections

#### 8. **cmake/SourceFiles.cmake** (~209 lines)
**Lines 872-1080**
- All module source file lists:
  - UTIL_SRCS
  - CRYPTO_SRCS
  - PLATFORM_SRCS
  - SIMD_SRCS
  - VIDEO_SRCS
  - AUDIO_SRCS
  - NETWORK_SRCS
  - CORE_SRCS
  - DATA_STRUCTURES_SRCS

**Functions to create:**
- `collect_source_files()` - Populates all *_SRCS variables

**Prerequisites**:
- Platform detection complete
- `USE_MUSL` known

**Note**: Could split into separate modules per library (SourceFilesUtil.cmake, etc.), but probably too granular

---

#### 9. **cmake/Libraries.cmake** (~340 lines)
**Lines 1083-1415**
- `create_ascii_chat_module()` macro
- All library target creation
- Library linking configuration
- Unified library targets (static/shared)

**Functions to create:**
- Macro remains as-is (macros can't be in functions)
- `create_all_libraries()` - Calls macro for each module
- `configure_library_linking()` - Sets up dependencies

**Prerequisites**:
- All *_SRCS variables populated
- Platform detection complete

---

#### 10. **cmake/Executables.cmake** (~100 lines)
**Lines 1456-1556**
- Main executable target
- Dead code elimination
- musl linking
- macOS Info.plist embedding

**Functions to create:**
- `create_main_executable()` - Sets up ascii-chat executable

**Prerequisites**:
- All libraries created
- `USE_MUSL` known

---

#### 11. **cmake/Tests.cmake** (~249 lines)
**Lines 1566-1814**
- Test framework setup
- Test dependency linking (complex!)
- Test executable creation
- Test targets

**Functions to create:**
- `configure_tests()` - Full test setup if BUILD_TESTS and CRITERION_FOUND

**Prerequisites**:
- Libraries created
- CRITERION_FOUND
- `BUILD_TESTS` option

**Note**: This is complex due to platform-specific linking requirements

---

#### 12. **cmake/CustomTargets.cmake** (~148 lines)
**Lines 1826-1973**
- Format targets (clang-format)
- Static analysis targets (clang-tidy, scan-build)
- Clean targets

**Functions to create:**
- `add_format_targets()` - clang-format setup
- `add_static_analysis_targets()` - clang-tidy, scan-build

**Prerequisites**: Runs near end (after source files known)

---

#### 13. **cmake/StatusMessages.cmake** (~152 lines)
**Lines 1990-2142**
- Final configuration status output
- Build command help

**Functions to create:**
- `print_build_status()` - All status messages

**Prerequisites**: Runs at very end (after everything configured)

---

## Recommended Implementation Order

### Phase 1: Low-Risk Extractions
1. ✅ **StatusMessages.cmake** - Pure output, no dependencies
2. ✅ **CustomTargets.cmake** - Self-contained, minimal dependencies
3. ✅ **BuildConfiguration.cmake** - Well-defined inputs/outputs

### Phase 2: Medium-Risk Extractions
4. ✅ **CompilerDetection.cmake** - Clear interface, tested pattern
5. ✅ **PlatformDetection.cmake** - Self-contained detection logic
6. ✅ **OptionsConfiguration.cmake** - Builds on existing patterns

### Phase 3: Higher-Risk (Requires Careful Testing)
7. ⚠️ **SourceFiles.cmake** - Large but straightforward
8. ⚠️ **Libraries.cmake** - Complex but isolated
9. ⚠️ **Executables.cmake** - Depends on libraries

### Phase 4: Most Complex (Requires Extensive Testing)
10. ⚠️ **Tests.cmake** - Most complex linking logic
11. ⚠️ **EarlySetup.cmake** - Must run before project()
12. ⚠️ **MuslPreProject.cmake** - Must run before project(), critical

### Phase 5: Project Setup (Keep Minimal)
13. ✅ **ProjectConfiguration.cmake** - Thin wrapper around project() call

---

## New CMakeLists.txt Structure (After Full Modularization)

```cmake
cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Early Configuration (BEFORE project())
# =============================================================================
include(cmake/EarlySetup.cmake)
setup_dependency_cache()
configure_vcpkg_toolchain()
configure_build_generator()

include(cmake/MuslPreProject.cmake)
configure_musl_pre_project()

# =============================================================================
# Project Declaration
# =============================================================================
# Set languages based on platform
if(APPLE)
    set(PROJECT_LANGUAGES C OBJC)
else()
    set(PROJECT_LANGUAGES C)
endif()

project(ascii-chat
    VERSION 0.1.0
    DESCRIPTION "Real-time terminal-based video chat with ASCII art conversion"
    HOMEPAGE_URL "https://github.com/zfogg/ascii-chat"
    LANGUAGES ${PROJECT_LANGUAGES}
)

# =============================================================================
# Post-Project Configuration
# =============================================================================
include(cmake/ProjectConfiguration.cmake)
configure_project_setup()

include(cmake/CompilerDetection.cmake)
detect_and_configure_compiler()
setup_compile_commands_json()

include(cmake/BuildConfiguration.cmake)
configure_terminal_colors()
detect_cpu_cores()
configure_c_standard()

include(cmake/PlatformDetection.cmake)
detect_platform()

include(cmake/OptionsConfiguration.cmake)
configure_mimalloc_option()
configure_musl_options()

# =============================================================================
# Existing Modular Includes
# =============================================================================
include(cmake/CCache.cmake)
include(cmake/SIMD.cmake)
include(cmake/CRC32.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/CompilerFlags.cmake)
include(cmake/WindowsWorkarounds.cmake)
include(cmake/Version.cmake)
include(cmake/Mimalloc.cmake)
include(cmake/MuslDependencies.cmake)
include(cmake/Dependencies.cmake)
include(cmake/PostBuild.cmake)

# =============================================================================
# Build Type Configuration
# =============================================================================
# (Inline logic for build types, or extract to BuildTypeConfiguration.cmake)

# =============================================================================
# Source Files and Libraries
# =============================================================================
include(cmake/SourceFiles.cmake)
collect_source_files()

include(cmake/Libraries.cmake)
create_all_libraries()

# =============================================================================
# Executables and Tests
# =============================================================================
include(cmake/Executables.cmake)
create_main_executable()

include(cmake/Tests.cmake)
configure_tests()

# =============================================================================
# Custom Targets and Status
# =============================================================================
include(cmake/CustomTargets.cmake)
add_format_targets()
add_static_analysis_targets()

include(cmake/StatusMessages.cmake)
print_build_status()
```

**Estimated Final CMakeLists.txt Size**: ~200-300 lines (down from 2,143!)

---

## Benefits

1. **Maintainability**: Each module has a single responsibility
2. **Readability**: Main CMakeLists.txt shows high-level flow
3. **Testability**: Can test modules independently
4. **Reusability**: Modules can be reused in other projects
5. **Documentation**: Each module can have detailed docs

---

## Risks & Mitigations

### Risk 1: Execution Order Issues
- **Mitigation**: Clear documentation about prerequisites
- **Mitigation**: Keep pre-project() code inline initially

### Risk 2: Variable Scope Issues
- **Mitigation**: Use functions with explicit PARENT_SCOPE
- **Mitigation**: Document all input/output variables

### Risk 3: Breaking Existing Workflows
- **Mitigation**: Implement incrementally (Phase 1-5)
- **Mitigation**: Test after each phase

### Risk 4: Increased Complexity (Too Many Files)
- **Mitigation**: Group related modules (e.g., all detection in one module)
- **Mitigation**: Keep main CMakeLists.txt as index/table of contents

---

## Alternative: Hybrid Approach

Keep most critical sections inline, extract only:
- StatusMessages.cmake (~152 lines)
- CustomTargets.cmake (~148 lines)
- SourceFiles.cmake (~209 lines)
- Tests.cmake (~249 lines)

**Result**: ~1,385 lines in CMakeLists.txt (35% reduction, lower risk)

---

## Recommendation

**Start with Hybrid Approach** (Phase 1 + StatusMessages + CustomTargets + SourceFiles):
- Quick wins with low risk
- ~35% reduction in main file size
- Easy to test and verify

**Then evaluate**:
- If successful, continue with Phase 2-3
- If issues arise, stop and refine

