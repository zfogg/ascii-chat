cmake_minimum_required(VERSION 3.16)

# Set up vcpkg toolchain if available (must be before project())
# Skip vcpkg for MinGW builds as vcpkg libraries are built for MSVC
if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT USE_MINGW)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        message(STATUS "Using vcpkg toolchain from environment: $ENV{VCPKG_ROOT}")
    endif()
endif()



# Force Ninja generator on Windows for faster builds
if(WIN32 AND NOT CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Ninja" CACHE STRING "Build system generator")
    message(STATUS "Using Ninja generator for faster Windows builds")
endif()

# Speed up CMake's compiler tests by avoiding linking issues
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(ascii-chat LANGUAGES C CXX)

# Generate compile_commands.json for IDE/tool integration (clangd, VSCode, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure MinGW mode for any compiler
if(WIN32 AND USE_MINGW)
    message(STATUS "Configuring ${CMAKE_C_COMPILER_ID} for MinGW mode")
    
    # Set MinGW target and system paths for Clang (GCC doesn't need this)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
        set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)
        
        # Find MinGW system headers - check common installation paths
        set(MINGW_INCLUDE_PATHS
            "$ENV{USERPROFILE}/scoop/apps/gcc/current/include"  # Scoop GCC (current user)
            "$ENV{USERPROFILE}/scoop/apps/gcc/current/x86_64-w64-mingw32/include"  # Scoop GCC target-specific
            "$ENV{SCOOP}/apps/gcc/current/include"  # Scoop global install
            "$ENV{SCOOP}/apps/gcc/current/x86_64-w64-mingw32/include"  # Scoop global target-specific
            "C:/mingw64/include"  # MSYS2 MinGW64
            "C:/msys64/mingw64/include"  # MSYS2 full install
            "/mingw64/include"  # Unix-style path
            "${CMAKE_C_COMPILER}/../include"  # Relative to compiler
            "${CMAKE_C_COMPILER}/../../x86_64-w64-mingw32/include"  # GCC target include
        )
        
        foreach(INCLUDE_PATH IN LISTS MINGW_INCLUDE_PATHS)
            if(EXISTS "${INCLUDE_PATH}/stdio.h")
                include_directories(SYSTEM "${INCLUDE_PATH}")
                message(STATUS "Found MinGW system headers at: ${INCLUDE_PATH}")
                break()
            endif()
        endforeach()
    endif()
    
    # Use MinGW linker for all compilers
    set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_C_COMPILER} <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_COMPILER} <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
    
    # Override CMake's Windows-specific compile flags that are set by the platform module
    # These get added by CMake's Windows-Clang.cmake platform file
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreaded "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDLL "")
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebug "")  
    set(CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebugDLL "")
    set(CMAKE_CXX_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreaded "")
    set(CMAKE_CXX_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDLL "")
    set(CMAKE_CXX_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebug "")  
    set(CMAKE_CXX_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_MultiThreadedDebugDLL "")
    
    # Clear the MSVC runtime library setting that adds -D_DEBUG -D_DLL -D_MT
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")
    
    # Override all the flag variables that CMake sets for Windows
    set(CMAKE_C_FLAGS "" CACHE STRING "C flags" FORCE)
    set(CMAKE_CXX_FLAGS "" CACHE STRING "CXX flags" FORCE) 
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g -Wall -Wextra -DDEBUG" CACHE STRING "C Debug flags" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra -DDEBUG" CACHE STRING "CXX Debug flags" FORCE)
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C Release flags" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "CXX Release flags" FORCE)
    
    message(STATUS "MinGW build using clean compiler flags (no MSVC runtime flags)")
endif()

# =============================================================================
# Configuration
# =============================================================================

# Detect CPU cores for parallel builds
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    execute_process(COMMAND sysctl -n hw.logicalcpu OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    execute_process(COMMAND nproc OUTPUT_VARIABLE CPU_CORES OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(WIN32)
    # Windows: Use environment variable or wmic
    if(DEFINED ENV{NUMBER_OF_PROCESSORS})
        set(CPU_CORES $ENV{NUMBER_OF_PROCESSORS})
    else()
        execute_process(COMMAND wmic cpu get NumberOfLogicalProcessors /value 
                       OUTPUT_VARIABLE CPU_INFO OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "NumberOfLogicalProcessors=([0-9]+)" _ ${CPU_INFO})
        if(CMAKE_MATCH_1)
            set(CPU_CORES ${CMAKE_MATCH_1})
        else()
            set(CPU_CORES 4)
        endif()
    endif()
else()
    set(CPU_CORES 4)
endif()

# Set parallel build level automatically if not already set
if(NOT DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} ${CPU_CORES})
endif()
message(STATUS "Parallel build jobs: ${CPU_CORES}")

# C standard selection - use latest C23 for future-proofing
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  # Use pure C23, not GNU extensions


# Build type (matches Makefile modes)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Dev, Release, Coverage" FORCE)
endif()

# Valid build types (matching Makefile)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Dev" "Release" "Coverage")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Platform Detection
# =============================================================================

# Detect platform specifics like the Makefile
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_DARWIN TRUE)
    set(PLATFORM_POSIX TRUE)
    
    # macOS SDK configuration (matches Makefile)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE SDKROOT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
        OUTPUT_VARIABLE CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    # Apple Silicon and Rosetta detection
    execute_process(
        COMMAND sysctl -n hw.optional.arm64
        OUTPUT_VARIABLE IS_APPLE_SILICON
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND sysctl -n sysctl.proc_translated
        OUTPUT_VARIABLE IS_ROSETTA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    if(NOT IS_APPLE_SILICON)
        set(IS_APPLE_SILICON 0)
    endif()
    if(NOT IS_ROSETTA)
        set(IS_ROSETTA 0)
    endif()
    
    # Force arm64 when building natively on Apple Silicon
    if(IS_APPLE_SILICON EQUAL 1 AND NOT IS_ROSETTA EQUAL 1)
        set(CMAKE_OSX_ARCHITECTURES arm64)
    endif()
    
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX TRUE)
    set(PLATFORM_POSIX TRUE)
    
elseif(WIN32)
    add_definitions(-D_WIN32 -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_WINDOWS TRUE)
    
    # Detect Windows architecture generically
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
        set(PLATFORM_WINDOWS_ARM64 TRUE)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
        set(PLATFORM_WINDOWS_ARM TRUE)
    else()
        set(PLATFORM_WINDOWS_X64 TRUE)
    endif()
else()
    set(PLATFORM_POSIX TRUE)
endif()

# Enable GNU extensions for POSIX functions (matches Makefile)
if(PLATFORM_POSIX AND NOT WIN32)
    add_definitions(-D_GNU_SOURCE)
endif()

# =============================================================================
# Compiler Flags (matching Makefile logic)
# =============================================================================

# Base warning flags for all non-MSVC compilers
if(NOT MSVC)
    add_compile_options(-Wall -Wextra)
endif()

# Platform-specific compiler flags
if(MSVC)
    add_compile_options(/W4)
    # Enable C11 atomic support in MSVC and disable warnings
    add_compile_options(/experimental:c11atomics)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
elseif(PLATFORM_DARWIN)
    # macOS specific flags (matches Makefile)
    if(SDKROOT AND CLANG_RESOURCE_DIR)
        add_compile_options(-isysroot ${SDKROOT} -isystem ${CLANG_RESOURCE_DIR}/include)
    endif()
endif()

# Include directories (matches Makefile)
include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# Build type specific flags (matching Makefile logic)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug mode WITH sanitizers (default, safe)
    add_definitions(-DDEBUG_MEMORY -DDEBUG_NETWORK)
    if(MSVC)
        add_compile_options(/Od /Zi)
        add_definitions(-DDEBUG)
    elseif(WIN32 AND USE_MINGW)
        # MinGW mode already has flags set - skip to avoid conflicts
        message(STATUS "Skipping debug flag setup for MinGW (already configured)")
    else()
        add_compile_options(-g -O0 -DDEBUG)
        
        # Always use sanitizers in Debug mode
        if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR (CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32))
            add_compile_options(-fsanitize=address,undefined)
            add_link_options(-fsanitize=address,undefined)
            
            # On Windows with Clang, copy ASAN runtime DLL to output directory
            if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
                # Find the ASAN DLL in the Clang installation
                get_filename_component(CLANG_DIR ${CMAKE_C_COMPILER} DIRECTORY)
                get_filename_component(CLANG_ROOT ${CLANG_DIR} DIRECTORY)
                file(GLOB_RECURSE ASAN_DLL "${CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
                if(ASAN_DLL)
                    # Copy ASAN DLL to build output directory
                    list(GET ASAN_DLL 0 ASAN_DLL_PATH)
                    file(COPY ${ASAN_DLL_PATH} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
                    message(STATUS "Copied ASAN runtime for Debug build: ${ASAN_DLL_PATH}")
                endif()
            endif()
        endif()
        
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
            # CodeView debug format is only supported by Clang on Windows (native mode)
            add_compile_options(-gcodeview)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Dev")
    # Dev mode - debug WITHOUT sanitizers (faster iteration)
    add_definitions(-DDEBUG_MEMORY -DDEBUG_NETWORK)
    if(MSVC)
        add_compile_options(/Od /Zi)
        add_definitions(-DDEBUG)
    elseif(WIN32 AND USE_MINGW)
        # MinGW mode already has flags set - skip to avoid conflicts
        message(STATUS "Skipping dev flag setup for MinGW (already configured)")
    else()
        add_compile_options(-g -O0 -DDEBUG)
        # No sanitizers in Dev mode for faster compilation/runtime
        
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
            # CodeView debug format is only supported by Clang on Windows (native mode)
            add_compile_options(-gcodeview)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    add_definitions(-DDEBUG_MEMORY -DCOVERAGE_BUILD)
    if(NOT MSVC)
        add_compile_options(-g -O0 --coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DNDEBUG)
    if(MSVC)
        add_compile_options(/O2)
    else()
        # CPU-aware optimization flags (matches Makefile)
        add_compile_options(-O3 -funroll-loops -fstrict-aliasing -ftree-vectorize -fomit-frame-pointer -pipe)
        if(PLATFORM_DARWIN)
            if(IS_ROSETTA EQUAL 1)
                add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
            elseif(IS_APPLE_SILICON EQUAL 1)
                add_compile_options(-march=native -mcpu=native -ffast-math -ffp-contract=fast)
            else()
                add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
            endif()
        elseif(PLATFORM_LINUX)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
                add_compile_options(-mcpu=native -ffp-contract=fast -ffinite-math-only)
            else()
                add_compile_options(-march=native -ffp-contract=fast -ffinite-math-only)
            endif()
        else()
            add_compile_options(-ffp-contract=fast)
        endif()
        
        if(NOT WIN32)
            add_compile_options(-flto -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-trapping-math -falign-loops=32 -falign-functions=32)
            add_link_options(-flto)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Sanitize")
    add_definitions(-DDEBUG_MEMORY)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR (CMAKE_C_COMPILER_ID MATCHES "GNU" AND NOT WIN32))
        add_compile_options(-g -O0 -fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
        
        # On Windows with Clang, copy ASAN runtime DLL to output directory
        if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Find the ASAN DLL in the Clang installation
            get_filename_component(CLANG_DIR ${CMAKE_C_COMPILER} DIRECTORY)
            get_filename_component(CLANG_ROOT ${CLANG_DIR} DIRECTORY)
            file(GLOB_RECURSE ASAN_DLL "${CLANG_ROOT}/lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
            if(ASAN_DLL)
                # Copy ASAN DLL to build output directory
                list(GET ASAN_DLL 0 ASAN_DLL_PATH)
                file(COPY ${ASAN_DLL_PATH} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
                message(STATUS "Copied ASAN runtime: ${ASAN_DLL_PATH}")
            else()
                message(WARNING "Could not find ASAN runtime DLL in Clang installation")
            endif()
        endif()
    else()
        # Fallback for compilers without sanitizer support
        add_compile_options(-g -O0)
        message(WARNING "AddressSanitizer not available for this compiler/platform combination")
    endif()
endif()

# Hardware Acceleration Detection (cross-platform including Windows ARM)
# =============================================================================

include(CheckCSourceCompiles)
include(CheckCSourceRuns)

# User override controls
set(SIMD_MODE "auto" CACHE STRING "SIMD mode: auto, sse2, ssse3, avx2, avx512, neon, sve")
set_property(CACHE SIMD_MODE PROPERTY STRINGS "auto" "sse2" "ssse3" "avx2" "avx512" "neon" "sve")

# Initialize SIMD flags
set(ENABLE_SIMD_SSE2 FALSE)
set(ENABLE_SIMD_SSSE3 FALSE)
set(ENABLE_SIMD_AVX2 FALSE)
set(ENABLE_SIMD_AVX512 FALSE)
set(ENABLE_SIMD_NEON FALSE)
set(ENABLE_SIMD_SVE FALSE)

# Check for user-specified SIMD mode
if(NOT SIMD_MODE STREQUAL "auto")
    # Manual mode - only enable the specific architecture requested
    if(SIMD_MODE STREQUAL "sse2")
        set(ENABLE_SIMD_SSE2 TRUE)
    elseif(SIMD_MODE STREQUAL "ssse3")
        set(ENABLE_SIMD_SSSE3 TRUE)
    elseif(SIMD_MODE STREQUAL "avx2")
        set(ENABLE_SIMD_AVX2 TRUE)
    elseif(SIMD_MODE STREQUAL "avx512")
        set(ENABLE_SIMD_AVX512 TRUE)
    elseif(SIMD_MODE STREQUAL "neon")
        set(ENABLE_SIMD_NEON TRUE)
    elseif(SIMD_MODE STREQUAL "sve")
        set(ENABLE_SIMD_SVE TRUE)
    endif()
else()
    # Auto-detect SIMD capabilities (cross-platform)
    
    # Windows-specific detection
    if(WIN32)
        # Check processor architecture on Windows
        # CMAKE_SYSTEM_PROCESSOR on Windows: AMD64, x86, ARM64, ARM
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
            # Windows on ARM64 - ARMv8 always has NEON
            set(ENABLE_SIMD_NEON TRUE)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
            # Windows on ARM32 - NEON is optional in ARMv7
            # Try compile test to check for NEON
            check_c_source_compiles("
                #include <arm_neon.h>
                int main() {
                    uint8x16_t a = vdupq_n_u8(0);
                    return 0;
                }
            " CAN_COMPILE_NEON)
            if(CAN_COMPILE_NEON)
                set(ENABLE_SIMD_NEON TRUE)
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            # Windows x86_64 - try compile tests for SIMD features
            # Try to compile and run CPUID check for x86
            # Note: Clang on Windows may have issues with check_c_source_runs, so we use compile checks instead
            if(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
                # Check from highest to lowest, stop when we find support
                # Higher instruction sets imply lower ones
                
                # Check for AVX512
                check_c_source_runs("
                    #include <intrin.h>
                    int main() {
                        int info[4];
                        __cpuidex(info, 7, 0);
                        if (info[1] & (1 << 16) && info[1] & (1 << 30)) // AVX512F and AVX512BW
                            return 0;
                        return 1;
                    }
                " HAS_AVX512_RUNTIME)
                
                if(HAS_AVX512_RUNTIME)
                    set(ENABLE_SIMD_AVX512 TRUE)
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                else()
                    # Check for AVX2
                    check_c_source_runs("
                        #include <intrin.h>
                        int main() {
                            int info[4];
                            __cpuidex(info, 7, 0);
                            if (info[1] & (1 << 5)) // AVX2
                                return 0;
                            return 1;
                        }
                    " HAS_AVX2_RUNTIME)
                    
                    if(HAS_AVX2_RUNTIME)
                        set(ENABLE_SIMD_AVX2 TRUE)
                        set(ENABLE_SIMD_SSSE3 TRUE)
                        set(ENABLE_SIMD_SSE2 TRUE)
                    else()
                        # Check for SSSE3
                        check_c_source_runs("
                            #include <intrin.h>
                            int main() {
                                int info[4];
                                __cpuid(info, 1);
                                if (info[2] & (1 << 9)) // SSSE3
                                    return 0;
                                return 1;
                            }
                        " HAS_SSSE3_RUNTIME)
                        
                        if(HAS_SSSE3_RUNTIME)
                            set(ENABLE_SIMD_SSSE3 TRUE)
                            set(ENABLE_SIMD_SSE2 TRUE)
                        else()
                            # Check for SSE2 (baseline for x86_64)
                            check_c_source_runs("
                                #include <intrin.h>
                                int main() {
                                    int info[4];
                                    __cpuid(info, 1);
                                    if (info[3] & (1 << 26)) // SSE2
                                        return 0;
                                    return 1;
                                }
                            " HAS_SSE2_RUNTIME)
                            
                            if(HAS_SSE2_RUNTIME)
                                set(ENABLE_SIMD_SSE2 TRUE)
                            endif()
                        endif()
                    endif()
                endif()
            else()
                # Cross-compiling or Clang on Windows, use compile-time checks with appropriate flags
                # Save current flags
                set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})
                
                # Test AVX2 with required flags
                set(CMAKE_REQUIRED_FLAGS "-mavx2")
                check_c_source_compiles("
                    #include <immintrin.h>
                    int main() { __m256i a = _mm256_setzero_si256(); return 0; }
                " CAN_COMPILE_AVX2)
                
                # Test SSSE3 with required flags
                set(CMAKE_REQUIRED_FLAGS "-mssse3")
                check_c_source_compiles("
                    #include <tmmintrin.h>
                    int main() { __m128i a = _mm_setzero_si128(); a = _mm_abs_epi8(a); return 0; }
                " CAN_COMPILE_SSSE3)
                
                # Test SSE2 with required flags
                set(CMAKE_REQUIRED_FLAGS "-msse2")
                check_c_source_compiles("
                    #include <emmintrin.h>
                    int main() { __m128i a = _mm_setzero_si128(); return 0; }
                " CAN_COMPILE_SSE2)
                
                # Restore flags
                set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
                
                # Enable all instruction sets that can be compiled
                if(CAN_COMPILE_SSE2)
                    set(ENABLE_SIMD_SSE2 TRUE)
                endif()
                if(CAN_COMPILE_SSSE3)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                endif()
                if(CAN_COMPILE_AVX2)
                    set(ENABLE_SIMD_AVX2 TRUE)
                endif()
            endif()
        endif()
        
    elseif(PLATFORM_DARWIN)
        # macOS detection (native)
        if(IS_APPLE_SILICON EQUAL 1)
            set(ENABLE_SIMD_NEON TRUE)
        elseif(IS_ROSETTA EQUAL 1)
            set(ENABLE_SIMD_SSSE3 TRUE)
        else()
            set(ENABLE_SIMD_SSSE3 TRUE)
        endif()
        
    elseif(PLATFORM_LINUX)
        # Linux detection
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            # Linux ARM64 - ARMv8 always has NEON
            set(ENABLE_SIMD_NEON TRUE)
            # Check for SVE (optional extension)
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                if(CPUINFO_CONTENT MATCHES "sve")
                    # SVE is available in addition to NEON
                    set(ENABLE_SIMD_SVE TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
            # Linux ARM32 - NEON is optional
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                if(CPUINFO_CONTENT MATCHES "neon")
                    set(ENABLE_SIMD_NEON TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            # Linux x86_64 - use /proc/cpuinfo
            if(EXISTS "/proc/cpuinfo")
                file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
                # Check from highest to lowest, set all implied instruction sets
                if(CPUINFO_CONTENT MATCHES "avx512f" AND CPUINFO_CONTENT MATCHES "avx512bw")
                    set(ENABLE_SIMD_AVX512 TRUE)
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "avx2")
                    set(ENABLE_SIMD_AVX2 TRUE)
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "ssse3")
                    set(ENABLE_SIMD_SSSE3 TRUE)
                    set(ENABLE_SIMD_SSE2 TRUE)
                elseif(CPUINFO_CONTENT MATCHES "sse2")
                    set(ENABLE_SIMD_SSE2 TRUE)
                endif()
            endif()
        endif()
    endif()
endif()

# Apply SIMD compile definitions and flags
if(ENABLE_SIMD_SSE2 OR ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2 OR ENABLE_SIMD_AVX512 OR ENABLE_SIMD_NEON OR ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT)
    
    # Prefer wider vector widths for SIMD-heavy workloads
    if(ENABLE_SIMD_AVX2)
        add_compile_options(-mprefer-vector-width=256)
    endif()
endif()

# Apply specific SIMD flags
if(ENABLE_SIMD_SSE2)
    add_definitions(-DSIMD_SUPPORT_SSE2)
    if(NOT MSVC)
        if(WIN32)
            add_compile_options(-msse2 -mno-mmx)  # Disable MMX on Windows
        else()
            add_compile_options(-msse2)
        endif()
    endif()
endif()

if(ENABLE_SIMD_SSSE3)
    add_definitions(-DSIMD_SUPPORT_SSSE3)
    if(NOT MSVC)
        if(WIN32)
            add_compile_options(-mssse3 -mno-mmx)  # Disable MMX on Windows
        else()
            add_compile_options(-mssse3)
        endif()
    endif()
endif()

if(ENABLE_SIMD_AVX2)
    add_definitions(-DSIMD_SUPPORT_AVX2)
    if(NOT MSVC)
        if(WIN32)
            add_compile_options(-mavx2 -mno-mmx)  # Disable MMX on Windows
        else()
            add_compile_options(-mavx2)
        endif()
    elseif(MSVC)
        add_compile_options(/arch:AVX2)
    endif()
endif()

if(ENABLE_SIMD_AVX512)
    add_definitions(-DSIMD_SUPPORT_AVX512)
    if(NOT MSVC)
        add_compile_options(-mavx512f -mavx512bw)
    elseif(MSVC)
        add_compile_options(/arch:AVX512)
    endif()
endif()

if(ENABLE_SIMD_NEON)
    add_definitions(-DSIMD_SUPPORT_NEON)
    # Windows ARM64 with MSVC or Clang needs proper arch flags
    if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
            # Clang on Windows ARM might need explicit flags
            add_compile_options(-march=armv8-a+simd)
        endif()
        # MSVC on ARM automatically enables NEON
    endif()
endif()

if(ENABLE_SIMD_SVE)
    add_definitions(-DSIMD_SUPPORT_SVE)
    if(NOT MSVC)
        add_compile_options(-march=armv8-a+sve)
    endif()
endif()

# CRC32 Hardware Acceleration
set(CRC32_HW "auto" CACHE STRING "CRC32 hardware acceleration: auto, on, off")
set(ENABLE_CRC32_HW FALSE)

if(CRC32_HW STREQUAL "on")
    set(ENABLE_CRC32_HW TRUE)
elseif(CRC32_HW STREQUAL "auto")
    if(WIN32)
        # Windows CRC32 detection
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64")
            # Try to detect CRC32 support on ARM
            # ARMv8-A includes optional CRC32, but not all implementations have it
            if(NOT CMAKE_CROSSCOMPILING)
                # Try runtime detection for ARM CRC32
                check_c_source_runs("
                    #include <arm_acle.h>
                    #include <stdint.h>
                    int main() {
                        uint32_t crc = 0;
                        uint8_t data = 0x42;
                        crc = __crc32b(crc, data);
                        return 0;
                    }
                " HAS_ARM_CRC32_RUNTIME)
                if(HAS_ARM_CRC32_RUNTIME)
                    set(ENABLE_CRC32_HW TRUE)
                endif()
            else()
                # Cross-compiling - try compile test
                check_c_source_compiles("
                    #include <arm_acle.h>
                    #include <stdint.h>
                    int main() {
                        uint32_t crc = 0;
                        uint8_t data = 0x42;
                        crc = __crc32b(crc, data);
                        return 0;
                    }
                " CAN_COMPILE_ARM_CRC32)
                if(CAN_COMPILE_ARM_CRC32)
                    set(ENABLE_CRC32_HW TRUE)
                endif()
            endif()
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            # Check for SSE4.2 support on x86_64
            # Note: Clang on Windows has issues with check_c_source_runs, so check SIMD flags first
            if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2 OR ENABLE_SIMD_AVX512)
                # Assume SSE4.2 if we have SSSE3 or better
                set(ENABLE_CRC32_HW TRUE)
            elseif(NOT CMAKE_CROSSCOMPILING AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
                check_c_source_runs("
                    #include <intrin.h>
                    int main() {
                        int info[4];
                        __cpuid(info, 1);
                        if (info[2] & (1 << 20)) // SSE4.2
                            return 0;
                        return 1;
                    }
                " HAS_SSE42_RUNTIME)
                if(HAS_SSE42_RUNTIME)
                    set(ENABLE_CRC32_HW TRUE)
                endif()
            endif()
        endif()
    elseif(PLATFORM_DARWIN)
        # macOS detection
        if(IS_APPLE_SILICON EQUAL 1)
            # Apple Silicon M1/M2/M3 all have CRC32
            set(ENABLE_CRC32_HW TRUE)
        else()
            # Intel Mac - check for SSE4.2
            execute_process(
                COMMAND sysctl -n hw.optional.sse4_2
                OUTPUT_VARIABLE HAS_SSE42_MAC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(HAS_SSE42_MAC EQUAL 1)
                set(ENABLE_CRC32_HW TRUE)
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM|ARM64")
        # Linux/Generic ARM - try to detect CRC32 support
        if(PLATFORM_LINUX AND EXISTS "/proc/cpuinfo")
            file(READ "/proc/cpuinfo" CPUINFO_CONTENT)
            if(CPUINFO_CONTENT MATCHES "crc32")
                set(ENABLE_CRC32_HW TRUE)
            endif()
        else()
            # Try compile test as fallback
            check_c_source_compiles("
                #include <arm_acle.h>
                #include <stdint.h>
                int main() {
                    uint32_t crc = 0;
                    uint8_t data = 0x42;
                    crc = __crc32b(crc, data);
                    return 0;
                }
            " CAN_COMPILE_ARM_CRC32)
            if(CAN_COMPILE_ARM_CRC32)
                set(ENABLE_CRC32_HW TRUE)
            endif()
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        # Linux x86_64 - assume SSE4.2 if we have SSSE3
        if(ENABLE_SIMD_SSSE3 OR ENABLE_SIMD_AVX2 OR ENABLE_SIMD_AVX512)
            set(ENABLE_CRC32_HW TRUE)
        endif()
    endif()
endif()

if(ENABLE_CRC32_HW)
    add_definitions(-DHAVE_CRC32_HW)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        if(NOT MSVC)
            add_compile_options(-msse4.2)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|ARM64|aarch64")
        # ARM CRC32 requires specific compiler flags
        if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
            add_compile_options(-march=armv8-a+crc)
        elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
            add_compile_options(-march=armv8-a+crc)
        endif()
        # MSVC on ARM should handle CRC32 automatically if available
    endif()
endif()

# =============================================================================
# Find Dependencies (matching Makefile pkg-config approach)
# =============================================================================

# Platform-specific package management and library linking
if(WIN32 AND NOT USE_MINGW)
    # Native Windows with MSVC/Clang - use vcpkg
    # Setup vcpkg paths if available
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT $ENV{VCPKG_ROOT})
        set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/x64-windows" ${CMAKE_PREFIX_PATH})
        include_directories("${VCPKG_ROOT}/installed/x64-windows/include")
        link_directories("${VCPKG_ROOT}/installed/x64-windows/lib")
        message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    endif()
    
    # Find packages using vcpkg (toolchain already configured at top of file)
    # Find zlib - vcpkg provides both debug and optimized libraries
    find_package(ZLIB REQUIRED)
    message(STATUS "Found ZLIB: ${ZLIB_VERSION_STRING}")
    # ZLIB_LIBRARIES from vcpkg contains generator expressions that automatically
    # select the debug library (zlibd.lib) for Debug builds and the optimized 
    # library (zlib.lib) for Release builds
    
    # Find libsodium - try finding it directly to avoid import issues
    find_library(LIBSODIUM_LIBRARY NAMES libsodium sodium
                PATHS "${VCPKG_ROOT}/installed/x64-windows/lib")
    find_path(LIBSODIUM_INCLUDE_DIR NAMES sodium.h 
             PATHS "${VCPKG_ROOT}/installed/x64-windows/include")
    if(LIBSODIUM_LIBRARY AND LIBSODIUM_INCLUDE_DIR)
        set(LIBSODIUM_LIBRARIES ${LIBSODIUM_LIBRARY})
        set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
        set(LIBSODIUM_FOUND TRUE)
        message(STATUS "Found libsodium: ${LIBSODIUM_LIBRARY}")
    else()
        message(WARNING "Could not find libsodium - will continue without encryption")
        set(LIBSODIUM_FOUND FALSE)
        set(LIBSODIUM_LIBRARIES "")
        set(LIBSODIUM_INCLUDE_DIRS "")
    endif()
elseif(WIN32 AND USE_MINGW)
    # MinGW on Windows - build with minimal dependencies for now
    message(STATUS "MinGW build - using minimal dependencies")
    
    # For MinGW, we'll build without external dependencies to keep it simple
    # This allows testing the core functionality without library complexity
    set(ZLIB_FOUND FALSE)
    set(ZLIB_LIBRARIES "")
    set(ZLIB_INCLUDE_DIRS "")
    
    set(LIBSODIUM_FOUND FALSE)
    set(LIBSODIUM_LIBRARIES "")
    set(LIBSODIUM_INCLUDE_DIRS "")
    
    set(PORTAUDIO_FOUND FALSE)
    set(PORTAUDIO_LIBRARIES "")
    set(PORTAUDIO_INCLUDE_DIRS "")
    
    message(STATUS "MinGW build configured without external dependencies")
else()
    # Unix/Linux/macOS use pkg-config (matches Makefile)
    find_package(PkgConfig REQUIRED)
    
    # Core dependencies (matching PKG_CONFIG_LIBS in Makefile)
    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)
    
    # Test dependencies (matching TEST_PKG_CONFIG_LIBS in Makefile)
    pkg_check_modules(CRITERION criterion)
endif()

# Windows SDK and MSVC detection for native Clang builds
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT USE_MINGW)
    message(STATUS "Setting up native Windows build with Clang")
    
    # Find Windows SDK
    set(WINDOWS_KIT_PATHS
        "C:/Program Files (x86)/Windows Kits/10"
        "C:/Program Files/Windows Kits/10"
        "$ENV{WindowsSdkDir}"
    )
    
    foreach(KIT_PATH IN LISTS WINDOWS_KIT_PATHS)
        if(EXISTS "${KIT_PATH}" AND NOT WINDOWS_KITS_DIR)
            set(WINDOWS_KITS_DIR "${KIT_PATH}")
            break()
        endif()
    endforeach()
    
    if(WINDOWS_KITS_DIR)
        # Find latest SDK version
        file(GLOB SDK_VERSIONS "${WINDOWS_KITS_DIR}/Lib/10.*")
        if(SDK_VERSIONS)
            list(SORT SDK_VERSIONS COMPARE NATURAL ORDER DESCENDING)
            list(GET SDK_VERSIONS 0 LATEST_SDK)
            get_filename_component(WINDOWS_SDK_VERSION ${LATEST_SDK} NAME)
            
            # Validate SDK has required components
            if(EXISTS "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/ucrt" AND
               EXISTS "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/x64")
                
                # Architecture detection
                if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                    set(WIN_ARCH x64)
                else()
                    set(WIN_ARCH x86)
                endif()
                
                # Add Windows SDK paths
                include_directories(
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/ucrt"
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/um"
                    "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}/shared"
                )
                
                link_directories(
                    "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/ucrt/${WIN_ARCH}"
                    "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}/um/${WIN_ARCH}"
                )
                
                message(STATUS "Found Windows SDK ${WINDOWS_SDK_VERSION}")
            endif()
        endif()
    endif()
    
    # Find MSVC runtime libraries for Clang
    set(VS_PATHS
        "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*"
        "C:/Program Files/Microsoft Visual Studio/2019/*/VC/Tools/MSVC/*"
        "C:/Program Files (x86)/Microsoft Visual Studio/*/VC/Tools/MSVC/*"
    )
    
    foreach(VS_PATTERN IN LISTS VS_PATHS)
        file(GLOB MSVC_VERSIONS "${VS_PATTERN}")
        if(MSVC_VERSIONS)
            list(SORT MSVC_VERSIONS COMPARE NATURAL ORDER DESCENDING)
            list(GET MSVC_VERSIONS 0 LATEST_MSVC)
            get_filename_component(MSVC_VERSION ${LATEST_MSVC} NAME)
            
            if(EXISTS "${LATEST_MSVC}/lib/${WIN_ARCH}")
                link_directories("${LATEST_MSVC}/lib/${WIN_ARCH}")
                message(STATUS "Found MSVC ${MSVC_VERSION}")
                break()
            endif()
        endif()
    endforeach()
endif()
# Find Windows-specific libraries (native mode only)
if(WIN32 AND NOT USE_MINGW)
    # Standard Windows system libraries for native builds
    set(WS2_32_LIB ws2_32)
    set(USER32_LIB user32)
    set(ADVAPI32_LIB advapi32)
    set(MF_LIB mf)
    set(MFPLAT_LIB mfplat)
    set(MFREADWRITE_LIB mfreadwrite)
    set(MFUUID_LIB mfuuid)
    set(OLE32_LIB ole32)
    
    # Find PortAudio - vcpkg provides appropriate library for each config
    find_package(portaudio CONFIG REQUIRED)
    set(PORTAUDIO_LIBRARIES portaudio)
    set(PORTAUDIO_FOUND TRUE)
elseif(WIN32 AND USE_MINGW)
    # MinGW uses different library names/linking
    set(WS2_32_LIB ws2_32)
    set(USER32_LIB user32)
    set(ADVAPI32_LIB advapi32)
    set(MF_LIB "")  # Media Foundation not used in MinGW
    set(MFPLAT_LIB "")
    set(MFREADWRITE_LIB "")
    set(MFUUID_LIB "")
    set(OLE32_LIB ole32)
else()
    # Platform-specific libraries (matching Makefile logic)
    if(PLATFORM_DARWIN)
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
        find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
        find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
        find_package(Curses REQUIRED)
    elseif(PLATFORM_LINUX)
        find_package(Curses REQUIRED)
        find_package(Threads REQUIRED)
        
        # Linux library search paths (matches Makefile)
        link_directories(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu)
        
        # Check for JACK (optional, matches Makefile)
        pkg_check_modules(JACK jack)
        
        # Additional Linux test dependencies (matching Makefile complex setup)
        if(CRITERION_FOUND)
            # Protobuf-C
            pkg_check_modules(PROTOBUF_C libprotobuf-c)
            if(NOT PROTOBUF_C_FOUND)
                find_library(PROTOBUF_C_LIBRARIES protobuf-c)
            endif()
            
            # Nanopb
            pkg_check_modules(NANOPB nanopb)
            if(NOT NANOPB_FOUND)
                find_library(NANOPB_LIBRARIES protobuf-nanopb PATHS /usr/lib/x86_64-linux-gnu)
            endif()
            
            # Boxfort (sandboxing for criterion)
            pkg_check_modules(BOXFORT boxfort)
            if(NOT BOXFORT_FOUND)
                find_library(BOXFORT_LIBRARIES boxfort)
            endif()
            
            # Optional: nanomsg, libgit2
            pkg_check_modules(NANOMSG nanomsg)
            pkg_check_modules(LIBGIT2 libgit2)
            
            # GSSAPI/Kerberos support
            pkg_check_modules(KRB5_GSSAPI krb5-gssapi)
            if(NOT KRB5_GSSAPI_FOUND)
                pkg_check_modules(KRB5_GSSAPI mit-krb5-gssapi)
            endif()
            if(NOT KRB5_GSSAPI_FOUND)
                pkg_check_modules(LIBSSH2 libssh2)
            endif()
        endif()
    endif()
endif()

# =============================================================================
# Include Directories (matching Makefile)
# =============================================================================

# Base include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
)

# Add dependency include directories (matching pkg-config approach)
if(WIN32)
    if(DEFINED ENV{VCPKG_ROOT})
        include_directories($ENV{VCPKG_ROOT}/installed/x64-windows/include)
    endif()
    # Add additional Windows include paths if found
    if(ZLIB_INCLUDE_DIRS)
        include_directories(${ZLIB_INCLUDE_DIRS})
    endif()
    if(LIBSODIUM_INCLUDE_DIRS)
        include_directories(${LIBSODIUM_INCLUDE_DIRS})
    endif()
else()
    # Use pkg-config flags (matches Makefile CFLAGS approach)
    if(ZLIB_CFLAGS_OTHER)
        add_compile_options(${ZLIB_CFLAGS_OTHER})
    endif()
    if(LIBSODIUM_CFLAGS_OTHER)
        add_compile_options(${LIBSODIUM_CFLAGS_OTHER})
    endif()
    if(PORTAUDIO_CFLAGS_OTHER)
        add_compile_options(${PORTAUDIO_CFLAGS_OTHER})
    endif()
    
    include_directories(
        ${ZLIB_INCLUDE_DIRS}
        ${LIBSODIUM_INCLUDE_DIRS}
        ${PORTAUDIO_INCLUDE_DIRS}
    )
endif()

# =============================================================================
# Source Files (matching Makefile structure)
# =============================================================================

# Common library sources (matching actual files in lib/)
set(LIB_COMMON_SRCS
    lib/common.c
    lib/logging.c
    lib/options.c
    lib/network.c
    lib/packet_queue.c
    lib/buffer_pool.c
    lib/hashtable.c
    lib/ringbuffer.c
    lib/compression.c
    lib/crypto.c
    lib/crc32_hw.c
    lib/mixer.c
    lib/os/audio.c
    lib/image2ascii/image.c
    lib/image2ascii/output_buffer.c
    lib/aspect_ratio.c
    lib/image2ascii/ascii.c
    lib/ansi_fast.c
    lib/os/webcam.c
    lib/palette.c
)

# Platform-specific sources (matching Makefile platform detection)
set(PLATFORM_SRCS_COMMON
    lib/platform/abstraction.c
)

if(WIN32)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        lib/platform/windows/thread.c
        lib/platform/windows/mutex.c
        lib/platform/windows/rwlock.c
        lib/platform/windows/cond.c
        lib/platform/windows/terminal.c
        lib/platform/windows/system.c
        lib/platform/windows/socket.c
        lib/os/windows/webcam_mediafoundation.c
    )
    
    # Add getopt for Windows (now in platform directory)
    if(EXISTS "${CMAKE_SOURCE_DIR}/lib/platform/windows/getopt.c")
        list(APPEND PLATFORM_SRCS lib/platform/windows/getopt.c)
    endif()
else()
    # POSIX platforms (Linux/macOS)
    set(PLATFORM_SRCS
        ${PLATFORM_SRCS_COMMON}
        lib/platform/posix/thread.c
        lib/platform/posix/mutex.c
        lib/platform/posix/rwlock.c
        lib/platform/posix/cond.c
        lib/platform/posix/terminal.c
        lib/platform/posix/system.c
        lib/platform/posix/socket.c
    )
    
    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_SRCS
            lib/os/macos/webcam_avfoundation.c
        )
    else()
        list(APPEND PLATFORM_SRCS
            lib/os/linux/webcam_v4l2.c
        )
    endif()
endif()

# SIMD sources (architecture-specific, matching Makefile logic)
set(SIMD_SRCS)

# Always include common SIMD files (matches Makefile)
list(APPEND SIMD_SRCS 
    lib/image2ascii/simd/ascii_simd.c
    lib/image2ascii/simd/ascii_simd_color.c
    lib/image2ascii/simd/common.c
)

# Architecture-specific SIMD sources based on detection
if(ENABLE_SIMD_SSE2)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/sse2.c)
endif()

if(ENABLE_SIMD_SSSE3)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/ssse3.c)
endif()

if(ENABLE_SIMD_AVX2)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/avx2.c)
    # Set specific compile flags for AVX2 files
    if(NOT MSVC)
        set_source_files_properties(lib/image2ascii/simd/avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
    endif()
endif()

if(ENABLE_SIMD_AVX512)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/avx512.c)
    if(NOT MSVC)
        set_source_files_properties(lib/image2ascii/simd/avx512.c PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512bw")
    endif()
endif()

if(ENABLE_SIMD_NEON)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/neon.c)
endif()

if(ENABLE_SIMD_SVE)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/sve.c)
    if(NOT MSVC)
        set_source_files_properties(lib/image2ascii/simd/sve.c PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve")
    endif()
endif()

# Combine all library sources
set(LIB_SRCS
    ${LIB_COMMON_SRCS}
    ${PLATFORM_SRCS}
    ${SIMD_SRCS}
)

# =============================================================================
# Create Library
# =============================================================================

add_library(ascii-chat-lib STATIC ${LIB_SRCS})

# Link library dependencies (matching Makefile linking order)
if(WIN32)
    target_link_libraries(ascii-chat-lib
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        ${PORTAUDIO_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${LIBSODIUM_LIBRARIES}
    )
else()
    # Build LDFLAGS systematically (matches Makefile logic)
    set(PLATFORM_LIBS)
    
    if(PLATFORM_DARWIN)
        list(APPEND PLATFORM_LIBS
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
            ${CURSES_LIBRARIES}
        )
    elseif(PLATFORM_LINUX)
        list(APPEND PLATFORM_LIBS
            ${CURSES_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
        )
        
        # Add JACK if available (matching Makefile)
        if(JACK_FOUND)
            list(APPEND PLATFORM_LIBS ${JACK_LIBRARIES})
        endif()
    endif()
    
    # Check if pkg-config already provides pthread (matches Makefile)
    string(FIND "${ZLIB_LDFLAGS};${LIBSODIUM_LDFLAGS};${PORTAUDIO_LDFLAGS}" "-lpthread" PTHREAD_PROVIDED)
    if(PTHREAD_PROVIDED EQUAL -1)
        find_package(Threads REQUIRED)
        list(APPEND PLATFORM_LIBS ${CMAKE_THREAD_LIBS_INIT})
    endif()
    
    target_link_libraries(ascii-chat-lib
        ${ZLIB_LIBRARIES}
        ${LIBSODIUM_LIBRARIES}
        ${PORTAUDIO_LIBRARIES}
        ${PLATFORM_LIBS}
        m  # math library
    )
endif()

# =============================================================================
# Executables
# =============================================================================

# Server executable - now modular
add_executable(ascii-chat-server 
    src/server/main.c
    src/server/client.c
    src/server/protocol.c
    src/server/stream.c
    src/server/render.c
    src/server/stats.c
)
target_link_libraries(ascii-chat-server ascii-chat-lib)

# Client executable
add_executable(ascii-chat-client
    src/client/main.c
    src/client/server.c
    src/client/protocol.c
    src/client/display.c
    src/client/capture.c
    src/client/audio.c
    src/client/keepalive.c
)
target_link_libraries(ascii-chat-client ascii-chat-lib)

# macOS Info.plist embedding (matches Makefile)
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(client PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

# =============================================================================
# Tests (matching Makefile test infrastructure)
# =============================================================================

# Test framework setup
if(CRITERION_FOUND)
    enable_testing()
    
    # Build test LDFLAGS systematically (matches Makefile complex setup)
    set(TEST_LDFLAGS ${CRITERION_LIBRARIES})
    
    if(PLATFORM_LINUX)
        # Add Linux-specific test dependencies (matching Makefile order)
        if(PROTOBUF_C_FOUND)
            list(APPEND TEST_LDFLAGS ${PROTOBUF_C_LIBRARIES})
        elseif(PROTOBUF_C_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${PROTOBUF_C_LIBRARIES})
        else()
            list(APPEND TEST_LDFLAGS protobuf-c)
        endif()
        
        if(NANOPB_FOUND)
            list(APPEND TEST_LDFLAGS ${NANOPB_LIBRARIES})
        elseif(NANOPB_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${NANOPB_LIBRARIES})
        else()
            list(APPEND TEST_LDFLAGS /usr/lib/x86_64-linux-gnu/libprotobuf-nanopb.a)
        endif()
        
        if(BOXFORT_FOUND)
            list(APPEND TEST_LDFLAGS ${BOXFORT_LIBRARIES})
        elseif(BOXFORT_LIBRARIES)
            list(APPEND TEST_LDFLAGS ${BOXFORT_LIBRARIES})
        else()
            list(APPEND TEST_LDFLAGS boxfort)
        endif()
        
        # Optional dependencies
        if(NANOMSG_FOUND)
            list(APPEND TEST_LDFLAGS ${NANOMSG_LIBRARIES})
        endif()
        if(LIBGIT2_FOUND)
            list(APPEND TEST_LDFLAGS ${LIBGIT2_LIBRARIES})
        endif()
        
        # GSSAPI/Kerberos support
        if(KRB5_GSSAPI_FOUND)
            list(APPEND TEST_LDFLAGS ${KRB5_GSSAPI_LIBRARIES})
        elseif(LIBSSH2_FOUND)
            list(APPEND TEST_LDFLAGS ${LIBSSH2_LIBRARIES})
        else()
            list(APPEND TEST_LDFLAGS gssapi_krb5 krb5 k5crypto com_err)
        endif()
        
        # Additional system libraries (matches Makefile)
        list(APPEND TEST_LDFLAGS ssl crypto ssh2 http_parser pcre2-8 dl resolv)
    elseif(PLATFORM_DARWIN)
        # macOS test linking (simpler, matches Makefile)
        if(NOT CRITERION_LIBRARIES)
            # Fallback for Homebrew
            list(APPEND TEST_LDFLAGS "-L/opt/homebrew/lib" criterion)
        endif()
    endif()
    
    # Find test files (excluding problematic ones, matches Makefile)
    file(GLOB_RECURSE TEST_SRCS_ALL tests/unit/*.c tests/integration/*.c tests/performance/*.c)
    set(TEST_EXCLUDES
        tests/unit/ascii_simd_test.c
        tests/integration/server_multiclient_test.c
        tests/integration/video_pipeline_test.c
    )
    
    set(TEST_SRCS)
    foreach(test_src IN LISTS TEST_SRCS_ALL)
        list(FIND TEST_EXCLUDES ${test_src} IS_EXCLUDED)
        if(IS_EXCLUDED EQUAL -1)
            list(APPEND TEST_SRCS ${test_src})
        endif()
    endforeach()
    
    # Create test executables (matches Makefile naming convention)
    foreach(test_src ${TEST_SRCS})
        # Transform test file paths to executable names with flattened structure
        # tests/unit/common_test.c -> test_unit_common
        # tests/integration/crypto_network_test.c -> test_integration_crypto_network
        get_filename_component(test_name ${test_src} NAME_WE)
        get_filename_component(test_dir ${test_src} DIRECTORY)
        get_filename_component(test_subdir ${test_dir} NAME)
        
        string(REPLACE "_test" "" test_base ${test_name})
        set(test_exe_name "test_${test_subdir}_${test_base}")
        
        # Add test executable
        add_executable(${test_exe_name} ${test_src})
        
        # Link test dependencies (order matters for linking)
        target_link_libraries(${test_exe_name}
            ascii-chat-lib
            ${TEST_LDFLAGS}
        )
        
        # Use release objects for performance tests (matches Makefile)
        if(test_subdir STREQUAL "performance")
            target_compile_options(${test_exe_name} PRIVATE ${CMAKE_CXX_FLAGS_RELEASE})
        endif()
        
        # Add to CTest
        add_test(NAME ${test_exe_name} COMMAND ${test_exe_name})
    endforeach()
    
    # Custom targets for different test modes (matches Makefile)
    add_custom_target(test_debug
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running tests in debug mode"
    )
    
    add_custom_target(test_release
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C Release
        COMMENT "Running tests in release mode"
    )
    
    # Overall test target
    add_custom_target(test_all
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        COMMENT "Running all tests"
    )
    
else()
    message(WARNING "Criterion testing framework not found. Tests will not be built.")
    message(STATUS "To enable tests on Linux: sudo apt-get install libcriterion-dev")
    message(STATUS "To enable tests on macOS: brew install criterion")
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS ascii-chat-server ascii-chat-client
    RUNTIME DESTINATION bin
)

# =============================================================================
# Custom Targets (matching Makefile utilities)
# =============================================================================

# Format target (matches Makefile format target)

# Format target - cross-platform version
file(GLOB_RECURSE ALL_SOURCE_FILES 
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/lib/*.c
    ${CMAKE_SOURCE_DIR}/lib/*.h
    ${CMAKE_SOURCE_DIR}/tests/*.c
    ${CMAKE_SOURCE_DIR}/tests/*.h
)

# Filter out third-party or generated files if needed
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX ".*\.m$")  # Exclude Objective-C files

add_custom_target(format
    COMMAND clang-format -i ${ALL_SOURCE_FILES}
    COMMENT "Formatting source code with clang-format"
    VERBATIM
)

# Format check target

# Clean all target (matches Makefile clean)
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/lib
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMENT "Cleaning all build artifacts"
)

# Dependency installation targets (matches Makefile deps targets)
if(PLATFORM_DARWIN)
    add_custom_target(deps
        COMMAND brew install portaudio libsodium criterion zlib coreutils || true
        COMMENT "Installing macOS dependencies via Homebrew"
    )
    add_custom_target(deps-test
        COMMAND brew install gcovr || true
        COMMENT "Installing macOS test dependencies"
        DEPENDS deps
    )
elseif(PLATFORM_LINUX)
    add_custom_target(deps
        COMMAND sudo apt-get update && sudo apt-get install -y build-essential pkg-config libportaudio2 portaudio19-dev libsodium-dev libcriterion-dev zlib1g-dev || true
        COMMENT "Installing Linux dependencies via apt"
    )
    add_custom_target(deps-test
        COMMAND sudo apt-get install -y lcov gcovr valgrind || true
        COMMENT "Installing Linux test dependencies"
        DEPENDS deps
    )
endif()

# =============================================================================
# Status Messages
# =============================================================================

# Status Messages (matching Makefile help output)
message(STATUS "")
message(STATUS "========================================")
message(STATUS "ASCII-Chat Configuration:")
message(STATUS "========================================")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Standard:        C${CMAKE_C_STANDARD}")
message(STATUS "  C Compiler:        ${CMAKE_C_COMPILER}")
message(STATUS "  Platform:          ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Processor:         ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  CPU Cores:         ${CPU_CORES}")
if(WIN32)
    if(WINDOWS_SDK_VERSION)
        message(STATUS "  Windows SDK:       ${WINDOWS_SDK_VERSION}")
    endif()
    if(PLATFORM_WINDOWS_ARM64)
        message(STATUS "  Architecture:      ARM64")
    elseif(PLATFORM_WINDOWS_ARM)
        message(STATUS "  Architecture:      ARM")
    else()
        message(STATUS "  Architecture:      x64")
    endif()
elseif(PLATFORM_DARWIN)
    message(STATUS "  Apple Silicon:     ${IS_APPLE_SILICON}")
    message(STATUS "  Rosetta:           ${IS_ROSETTA}")
endif()
message(STATUS "")
message(STATUS "Hardware Acceleration:")
message(STATUS "  SIMD Mode:         ${SIMD_MODE}")
message(STATUS "  SSE2:              ${ENABLE_SIMD_SSE2}")
message(STATUS "  SSSE3:             ${ENABLE_SIMD_SSSE3}")
message(STATUS "  AVX2:              ${ENABLE_SIMD_AVX2}")
message(STATUS "  AVX512:            ${ENABLE_SIMD_AVX512}")
message(STATUS "  NEON:              ${ENABLE_SIMD_NEON}")
message(STATUS "  SVE:               ${ENABLE_SIMD_SVE}")
message(STATUS "  CRC32 HW:          ${ENABLE_CRC32_HW}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  Tests:             ${CRITERION_FOUND}")
message(STATUS "  zlib:              ${ZLIB_FOUND}")
message(STATUS "  libsodium:         ${LIBSODIUM_FOUND}")
message(STATUS "  PortAudio:         Found")
if(JACK_FOUND)
    message(STATUS "  JACK:              ${JACK_FOUND}")
endif()
message(STATUS "")
message(STATUS "Build Commands:")
message(STATUS "  Configure Debug:   cmake -B build -DCMAKE_BUILD_TYPE=Debug     # With sanitizers (default)")
message(STATUS "  Configure Dev:     cmake -B build -DCMAKE_BUILD_TYPE=Dev       # Debug without sanitizers (faster)")
message(STATUS "  Configure Release: cmake -B build -DCMAKE_BUILD_TYPE=Release")
message(STATUS "  Configure Coverage:cmake -B build -DCMAKE_BUILD_TYPE=Coverage")
message(STATUS "  Build:             cmake --build build")
message(STATUS "  Run server:        ./build/bin/ascii-chat-server")
message(STATUS "  Run client:        ./build/bin/ascii-chat-client")
if(CRITERION_FOUND)
    message(STATUS "  Run tests:         ./tests/scripts/run_tests.sh")
    message(STATUS "  Run tests (CMake): cmake --build build --target test_all")
endif()
message(STATUS "  Format code:       cmake --build build --target format")
message(STATUS "  Install deps:      cmake --build build --target deps")
message(STATUS "========================================")
message(STATUS "")