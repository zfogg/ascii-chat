cmake_minimum_required(VERSION 3.16)

# For Windows with Clang, force GNU-like behavior
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
    set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)
endif()

project(ascii-chat LANGUAGES C CXX)

# =============================================================================
# Configuration
# =============================================================================

# Set C standard - use C17 for broader compiler support
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type: Debug, Release, RelWithDebInfo, MinSizeRel, Sanitize" FORCE)
endif()

# Valid build types
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel" "Sanitize")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# =============================================================================
# Platform Detection
# =============================================================================

if(WIN32)
    add_definitions(-D_WIN32 -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_WINDOWS TRUE)
else()
    set(PLATFORM_POSIX TRUE)
    # Enable GNU extensions for POSIX functions
    add_definitions(-D_GNU_SOURCE)
endif()

# =============================================================================
# Compiler Flags
# =============================================================================

# Platform-specific compiler flags
if(MSVC)
    add_compile_options(/W4)
    # Enable C11 atomic support in MSVC and disable warnings
    add_compile_options(/experimental:c11atomics)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    # GCC/Clang warning flags
    add_compile_options(-Wall -Wextra)
    
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        if(APPLE)
            # macOS specific flags
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
            add_compile_options(-isysroot ${SDKROOT} -isystem ${CLANG_RESOURCE_DIR}/include)
        endif()
    endif()
endif()

# Build type specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG_MEMORY -DDEBUG_NETWORK)
    if(MSVC)
        add_compile_options(/Od /Zi)
        add_definitions(-DDEBUG)
    else()
        add_compile_options(-g -O0 -DDEBUG)
        if(WIN32)
            add_compile_options(-gcodeview)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        add_compile_options(/O2)
        add_definitions(-DNDEBUG)
    else()
        add_compile_options(-O3 -DNDEBUG)
        if(NOT WIN32)
            add_compile_options(-flto)
            add_link_options(-flto)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(MSVC)
        add_compile_options(/O2 /Zi)
        add_definitions(-DNDEBUG)
    else()
        add_compile_options(-O2 -g -DNDEBUG)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    if(MSVC)
        add_compile_options(/O1)
        add_definitions(-DNDEBUG)
    else()
        add_compile_options(-Os -DNDEBUG)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Sanitize")
    add_compile_options(-g -O0 -fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    add_definitions(-DDEBUG_MEMORY)
endif()

# Architecture detection and SIMD flags
include(CheckCSourceCompiles)

# Check for SSE2
check_c_source_compiles("
    #include <emmintrin.h>
    int main() {
        __m128i a = _mm_setzero_si128();
        return 0;
    }
" HAS_SSE2)

# Check for SSSE3
check_c_source_compiles("
    #include <tmmintrin.h>
    int main() {
        __m128i a = _mm_setzero_si128();
        a = _mm_abs_epi8(a);
        return 0;
    }
" HAS_SSSE3)

# Check for AVX2
check_c_source_compiles("
    #include <immintrin.h>
    int main() {
        __m256i a = _mm256_setzero_si256();
        return 0;
    }
" HAS_AVX2)

# Check for ARM NEON
check_c_source_compiles("
    #include <arm_neon.h>
    int main() {
        uint8x16_t a = vdupq_n_u8(0);
        return 0;
    }
" HAS_NEON)

# =============================================================================
# Find Dependencies
# =============================================================================

# Platform-specific dependency handling
if(WIN32)
    # On Windows, try to find libraries directly or use vcpkg
    find_package(ZLIB)
    if(NOT ZLIB_FOUND)
        # Try to find zlib manually
        find_library(ZLIB_LIBRARY NAMES zlib zlibstatic zlibstaticd zlibd z)
        find_path(ZLIB_INCLUDE_DIR NAMES zlib.h)
        if(ZLIB_LIBRARY AND ZLIB_INCLUDE_DIR)
            set(ZLIB_FOUND TRUE)
            set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
            set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
        else()
            message(WARNING "zlib not found - will try to continue without compression")
            set(ZLIB_FOUND FALSE)
            set(ZLIB_LIBRARIES "")
            set(ZLIB_INCLUDE_DIRS "")
        endif()
    endif()
    
    # If still not found, disable compression
    if(NOT ZLIB_FOUND)
        set(ZLIB_LIBRARIES "")
        set(ZLIB_INCLUDE_DIRS "")
        add_definitions(-DNO_COMPRESSION)
    endif()
    
    find_package(sodium)
    if(NOT sodium_FOUND)
        find_library(LIBSODIUM_LIBRARY NAMES sodium libsodium)
        find_path(LIBSODIUM_INCLUDE_DIR NAMES sodium.h)
        if(LIBSODIUM_LIBRARY AND LIBSODIUM_INCLUDE_DIR)
            set(sodium_FOUND TRUE)
            set(LIBSODIUM_LIBRARIES ${LIBSODIUM_LIBRARY})
            set(LIBSODIUM_INCLUDE_DIRS ${LIBSODIUM_INCLUDE_DIR})
        else()
            message(WARNING "libsodium not found - will try to continue without encryption")
            set(sodium_FOUND FALSE)
        endif()
    endif()
else()
    # Unix/Linux/macOS use pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ZLIB REQUIRED zlib)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)
endif()

# Platform-specific package management
if(WIN32)
    # Detect vcpkg installation
    if(DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT $ENV{VCPKG_ROOT})
    elseif(EXISTS "C:/vcpkg")
        set(VCPKG_ROOT "C:/vcpkg")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg")
        set(VCPKG_ROOT "${CMAKE_SOURCE_DIR}/vcpkg")
    endif()
    
    if(VCPKG_ROOT)
        include(${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
        # Detect architecture
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(VCPKG_ARCH "x64-windows")
        else()
            set(VCPKG_ARCH "x86-windows")
        endif()
        set(CMAKE_PREFIX_PATH "${VCPKG_ROOT}/installed/${VCPKG_ARCH}")
        include_directories("${VCPKG_ROOT}/installed/${VCPKG_ARCH}/include")
        link_directories("${VCPKG_ROOT}/installed/${VCPKG_ARCH}/lib")
        message(STATUS "Using vcpkg from: ${VCPKG_ROOT}")
    endif()
    
    # Windows SDK setup (only for MSVC-compatible builds)
    if(MSVC OR (CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_C_COMPILER_TARGET))
        if(NOT DEFINED WINDOWS_SDK_VERSION)
            # Try to detect Windows SDK version
            file(GLOB SDK_VERSIONS "C:/Program Files (x86)/Windows Kits/10/Lib/*")
            if(SDK_VERSIONS)
                list(GET SDK_VERSIONS -1 LATEST_SDK)  # Get last (newest) version
                get_filename_component(WINDOWS_SDK_VERSION ${LATEST_SDK} NAME)
            endif()
        endif()
        
        if(WINDOWS_SDK_VERSION)
            set(WINDOWS_KITS_DIR "C:/Program Files (x86)/Windows Kits/10")
            set(WINDOWS_SDK_INCLUDE "${WINDOWS_KITS_DIR}/Include/${WINDOWS_SDK_VERSION}")
            set(WINDOWS_SDK_LIB "${WINDOWS_KITS_DIR}/Lib/${WINDOWS_SDK_VERSION}")
            
            # Add Windows SDK paths
            include_directories(
                "${WINDOWS_SDK_INCLUDE}/ucrt"
                "${WINDOWS_SDK_INCLUDE}/um"
                "${WINDOWS_SDK_INCLUDE}/shared"
            )
            
            link_directories(
                "${WINDOWS_SDK_LIB}/ucrt/x64"
                "${WINDOWS_SDK_LIB}/um/x64"
            )
            
            # Add MSVC library directory
            if(CMAKE_C_COMPILER_ID MATCHES "Clang")
                # For Clang on Windows, we need to find the MSVC libraries
                if(DEFINED ENV{VCToolsInstallDir})
                    set(MSVC_TOOLS_DIR $ENV{VCToolsInstallDir})
                else()
                    # Try common installation paths
                    file(GLOB MSVC_VERSIONS "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*")
                    if(MSVC_VERSIONS)
                        list(GET MSVC_VERSIONS -1 LATEST_MSVC)  # Get latest version
                        set(MSVC_TOOLS_DIR ${LATEST_MSVC})
                    endif()
                endif()
                
                if(MSVC_TOOLS_DIR AND EXISTS "${MSVC_TOOLS_DIR}/lib/x64")
                    link_directories("${MSVC_TOOLS_DIR}/lib/x64")
                    message(STATUS "Added MSVC library directory: ${MSVC_TOOLS_DIR}/lib/x64")
                endif()
            endif()
        endif()
    endif()
    
    # Find Windows-specific libraries
    if(CMAKE_C_COMPILER_ID MATCHES "MSVC" OR CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        # MSVC automatically finds system libraries
        set(WS2_32_LIB ws2_32)
        set(USER32_LIB user32)
        set(ADVAPI32_LIB advapi32)
        set(MF_LIB mf)
        set(MFPLAT_LIB mfplat)
        set(MFREADWRITE_LIB mfreadwrite)
        set(MFUUID_LIB mfuuid)
        set(OLE32_LIB ole32)
    else()
        # Clang/GCC need to find libraries explicitly
        find_library(WS2_32_LIB ws2_32)
        find_library(USER32_LIB user32)
        find_library(ADVAPI32_LIB advapi32)
        find_library(MF_LIB mf)
        find_library(MFPLAT_LIB mfplat)
        find_library(MFREADWRITE_LIB mfreadwrite)
        find_library(MFUUID_LIB mfuuid)
        find_library(OLE32_LIB ole32)
    endif()
    
    # PortAudio on Windows
    find_package(portaudio CONFIG REQUIRED)
    set(PORTAUDIO_LIBRARIES portaudio)
else()
    # POSIX systems use pkg-config
    pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
    
    # Platform-specific libraries
    if(APPLE)
        find_library(FOUNDATION_FRAMEWORK Foundation)
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
        find_library(COREMEDIA_FRAMEWORK CoreMedia)
        find_library(COREVIDEO_FRAMEWORK CoreVideo)
        find_package(Curses REQUIRED)
    elseif(UNIX)
        find_package(Curses REQUIRED)
        find_package(Threads REQUIRED)
        
        # Check for JACK (optional)
        pkg_check_modules(JACK jack)
    endif()
endif()

# =============================================================================
# Include Directories
# =============================================================================

include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/image2ascii
    ${CMAKE_SOURCE_DIR}/src/image2ascii/simd
)

# Add dependency include directories
if(WIN32)
    if(DEFINED ENV{VCPKG_ROOT})
        include_directories($ENV{VCPKG_ROOT}/installed/x64-windows/include)
    endif()
else()
    include_directories(
        ${ZLIB_INCLUDE_DIRS}
        ${LIBSODIUM_INCLUDE_DIRS}
        ${PORTAUDIO_INCLUDE_DIRS}
    )
endif()

# =============================================================================
# Source Files
# =============================================================================

# Common library sources
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
    lib/audio.c
    lib/image2ascii/image.c
    lib/aspect_ratio.c
    lib/image2ascii/ascii.c
    lib/ansi_fast.c
    lib/terminal_detect.c
    lib/webcam.c
    lib/palette.c
    lib/image2ascii/output_buffer.c
)

# Platform-specific sources
if(WIN32)
    set(PLATFORM_SRCS
        lib/platform/abstraction.c
        lib/platform/windows/thread.c
        lib/platform/windows/mutex.c
        lib/platform/windows/rwlock.c
        lib/platform/windows/cond.c
        lib/platform/windows/terminal.c
        lib/platform/windows/system.c
        lib/platform/windows/socket.c
        lib/webcam_windows.c
        lib/getopt_windows.c
    )
else()
    set(PLATFORM_SRCS
        lib/platform/abstraction.c
        lib/platform/posix/thread.c
        lib/platform/posix/mutex.c
        lib/platform/posix/rwlock.c
        lib/platform/posix/cond.c
        lib/platform/posix/terminal.c
        lib/platform/posix/system.c
        lib/platform/posix/socket.c
        lib/webcam_platform.c
    )
    
    if(APPLE)
        list(APPEND PLATFORM_SRCS
            lib/webcam_avfoundation.m
        )
    else()
        list(APPEND PLATFORM_SRCS
            lib/webcam_v4l2.c
        )
    endif()
endif()

# SIMD sources (architecture-specific)
set(SIMD_SRCS)

if(HAS_SSE2)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/sse2.c)
    add_definitions(-DHAS_SSE2)
endif()

if(HAS_SSSE3)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/ssse3.c)
    add_definitions(-DHAS_SSSE3)
endif()

if(HAS_AVX2)
    list(APPEND SIMD_SRCS lib/image2ascii/simd/avx2.c)
    add_definitions(-DHAS_AVX2)
    if(NOT MSVC)
        set_source_files_properties(lib/image2ascii/simd/avx2.c PROPERTIES COMPILE_FLAGS "-mavx2")
    endif()
endif()

if(HAS_NEON)
    list(APPEND SIMD_SRCS 
        lib/image2ascii/simd/neon.c
    )
    add_definitions(-DHAS_NEON)
endif()

# Add SIMD common files for all platforms
list(APPEND SIMD_SRCS 
    lib/image2ascii/simd/ascii_simd.c
    lib/image2ascii/simd/ascii_simd_color.c
    lib/image2ascii/simd/common.c
)

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

# Link library dependencies
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
    set(PLATFORM_LIBS)
    
    if(APPLE)
        list(APPEND PLATFORM_LIBS
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
            ${CURSES_LIBRARIES}
        )
    else()
        list(APPEND PLATFORM_LIBS
            ${CURSES_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
        )
        
        if(JACK_FOUND)
            list(APPEND PLATFORM_LIBS ${JACK_LIBRARIES})
        endif()
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

# Server executable
add_executable(server src/server.c)
target_link_libraries(server ascii-chat-lib)

# Client executable
add_executable(client src/client.c)
target_link_libraries(client ascii-chat-lib)

# =============================================================================
# Tests
# =============================================================================

# Find Criterion testing framework
if(NOT WIN32)
    pkg_check_modules(CRITERION criterion)
else()
    set(CRITERION_FOUND FALSE)
endif()

if(CRITERION_FOUND)
    enable_testing()
    
    # Find all test files
    file(GLOB_RECURSE TEST_SRCS tests/*.c)
    
    foreach(test_src ${TEST_SRCS})
        # Get test name from file path
        get_filename_component(test_name ${test_src} NAME_WE)
        get_filename_component(test_dir ${test_src} DIRECTORY)
        get_filename_component(test_subdir ${test_dir} NAME)
        
        # Create test executable name
        set(test_exe_name "test_${test_subdir}_${test_name}")
        
        # Add test executable
        add_executable(${test_exe_name} ${test_src})
        
        # Link test dependencies
        target_link_libraries(${test_exe_name}
            ascii-chat-lib
            ${CRITERION_LIBRARIES}
        )
        
        # Add to CTest
        add_test(NAME ${test_exe_name} COMMAND ${test_exe_name})
    endforeach()
    
    # Add custom target to run all tests
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${TEST_EXECUTABLES}
    )
else()
    message(WARNING "Criterion testing framework not found. Tests will not be built.")
endif()

# =============================================================================
# Installation
# =============================================================================

install(TARGETS server client
    RUNTIME DESTINATION bin
)

# =============================================================================
# Custom Targets
# =============================================================================

# Format target (requires clang-format)
add_custom_target(format
    COMMAND clang-format -i ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.h
            ${CMAKE_SOURCE_DIR}/lib/*.c ${CMAKE_SOURCE_DIR}/lib/*.h
    COMMENT "Formatting source code with clang-format"
)

# Clean all target
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/build
    COMMENT "Cleaning all build artifacts"
)

# =============================================================================
# Status Messages
# =============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "ASCII-Chat Configuration:")
message(STATUS "========================================")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler:        ${CMAKE_C_COMPILER}")
message(STATUS "  Platform:          ${CMAKE_SYSTEM_NAME}")
if(WIN32)
    message(STATUS "  Windows SDK:       ${WINDOWS_SDK_VERSION}")
endif()
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  SSE2:              ${HAS_SSE2}")
message(STATUS "  SSSE3:             ${HAS_SSSE3}")
message(STATUS "  AVX2:              ${HAS_AVX2}")
message(STATUS "  NEON:              ${HAS_NEON}")
message(STATUS "  Tests:             ${CRITERION_FOUND}")
message(STATUS "")
message(STATUS "Build Commands:")
message(STATUS "  Configure:         cmake -B build -DCMAKE_BUILD_TYPE=Debug|Release")
message(STATUS "  Build:             cmake --build build")
message(STATUS "  Run server:        ./build/bin/server")
message(STATUS "  Run client:        ./build/bin/client")
if(CRITERION_FOUND)
    message(STATUS "  Run tests:         cmake --build build --target run_tests")
endif()
message(STATUS "========================================")
message(STATUS "")