# =============================================================================
# libdatachannel - Standalone WebRTC Data Channels C/C++ Library
# =============================================================================
# This integrates libdatachannel for WebRTC P2P connections in ascii-chat.
#
# Source: https://github.com/paullouisageneau/libdatachannel
#
# libdatachannel Features:
#   - WebRTC DataChannels for binary/text data transport
#   - Full ICE (Interactive Connectivity Establishment) support
#   - STUN/TURN server support for NAT traversal
#   - SDP offer/answer negotiation
#   - Lightweight (~50k LOC vs Google's WebRTC 2M LOC)
#   - C API with C++ internals
#
# Build Strategy:
#   1. Try vcpkg first if USE_VCPKG=ON
#   2. If not found in vcpkg, fetch from repository at configure time
#   3. Initialize submodules (libsrtp, libjuice, usrsctp)
#   4. Build completely in deps cache (NOT in build/)
#   5. Import pre-built libraries as INTERFACE library
#   6. This prevents cluttering the main project with submodule targets
# =============================================================================

# =============================================================================
# Try vcpkg first if enabled
# =============================================================================
set(LIBDATACHANNEL_FOUND FALSE)

if(USE_VCPKG AND VCPKG_ROOT)
    message(STATUS "${BoldCyan}libdatachannel${ColorReset}: Checking vcpkg...")

    find_library(LIBDATACHANNEL_LIBRARY_RELEASE
        NAMES datachannel libdatachannel
        PATHS "${VCPKG_LIB_PATH}"
        NO_DEFAULT_PATH
    )
    find_library(LIBDATACHANNEL_LIBRARY_DEBUG
        NAMES datachannel libdatachannel
        PATHS "${VCPKG_DEBUG_LIB_PATH}"
        NO_DEFAULT_PATH
    )
    find_path(LIBDATACHANNEL_INCLUDE_DIR
        NAMES rtc/rtc.h rtc/rtc.hpp
        PATHS "${VCPKG_INCLUDE_PATH}"
        NO_DEFAULT_PATH
    )

    if(LIBDATACHANNEL_LIBRARY_RELEASE OR LIBDATACHANNEL_LIBRARY_DEBUG)
        set(LIBDATACHANNEL_FOUND TRUE)

        # Create INTERFACE library for vcpkg-provided libdatachannel
        add_library(libdatachannel INTERFACE)

        # Find libdatachannel dependencies from vcpkg
        find_library(LIBJUICE_LIBRARY_RELEASE NAMES juice libjuice PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
        find_library(LIBJUICE_LIBRARY_DEBUG NAMES juice libjuice PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)
        find_library(LIBUSRSCTP_LIBRARY_RELEASE NAMES usrsctp libusrsctp PATHS "${VCPKG_LIB_PATH}" NO_DEFAULT_PATH)
        find_library(LIBUSRSCTP_LIBRARY_DEBUG NAMES usrsctp libusrsctp PATHS "${VCPKG_DEBUG_LIB_PATH}" NO_DEFAULT_PATH)

        # Link libdatachannel and its dependencies
        if(LIBDATACHANNEL_LIBRARY_RELEASE AND LIBDATACHANNEL_LIBRARY_DEBUG)
            target_link_libraries(libdatachannel INTERFACE
                optimized ${LIBDATACHANNEL_LIBRARY_RELEASE}
                debug ${LIBDATACHANNEL_LIBRARY_DEBUG}
            )
        elseif(LIBDATACHANNEL_LIBRARY_RELEASE)
            target_link_libraries(libdatachannel INTERFACE ${LIBDATACHANNEL_LIBRARY_RELEASE})
        else()
            target_link_libraries(libdatachannel INTERFACE ${LIBDATACHANNEL_LIBRARY_DEBUG})
        endif()

        # Link dependencies
        if(LIBJUICE_LIBRARY_RELEASE AND LIBJUICE_LIBRARY_DEBUG)
            target_link_libraries(libdatachannel INTERFACE optimized ${LIBJUICE_LIBRARY_RELEASE} debug ${LIBJUICE_LIBRARY_DEBUG})
        elseif(LIBJUICE_LIBRARY_RELEASE)
            target_link_libraries(libdatachannel INTERFACE ${LIBJUICE_LIBRARY_RELEASE})
        elseif(LIBJUICE_LIBRARY_DEBUG)
            target_link_libraries(libdatachannel INTERFACE ${LIBJUICE_LIBRARY_DEBUG})
        endif()

        if(LIBUSRSCTP_LIBRARY_RELEASE AND LIBUSRSCTP_LIBRARY_DEBUG)
            target_link_libraries(libdatachannel INTERFACE optimized ${LIBUSRSCTP_LIBRARY_RELEASE} debug ${LIBUSRSCTP_LIBRARY_DEBUG})
        elseif(LIBUSRSCTP_LIBRARY_RELEASE)
            target_link_libraries(libdatachannel INTERFACE ${LIBUSRSCTP_LIBRARY_RELEASE})
        elseif(LIBUSRSCTP_LIBRARY_DEBUG)
            target_link_libraries(libdatachannel INTERFACE ${LIBUSRSCTP_LIBRARY_DEBUG})
        endif()

        # Platform-specific system libraries
        if(APPLE)
            target_link_libraries(libdatachannel INTERFACE
                "-framework Foundation"
                "-framework Security"
                ${ASCIICHAT_STATIC_LIBCXX_LIBS}
            )
        elseif(WIN32)
            target_link_libraries(libdatachannel INTERFACE ws2_32 iphlpapi bcrypt)
        else()
            target_link_libraries(libdatachannel INTERFACE $<$<NOT:$<BOOL:${USE_MUSL}>>:stdc++> pthread)
        endif()

        # OpenSSL for TURN credentials
        if(NOT TARGET OpenSSL::Crypto)
            find_package(OpenSSL REQUIRED)
        endif()
        target_link_libraries(libdatachannel INTERFACE OpenSSL::SSL OpenSSL::Crypto)

        target_include_directories(libdatachannel INTERFACE ${LIBDATACHANNEL_INCLUDE_DIR})

        message(STATUS "${BoldGreen}libdatachannel${ColorReset} found via vcpkg: ${LIBDATACHANNEL_LIBRARY_RELEASE}")
    else()
        message(STATUS "${BoldYellow}libdatachannel${ColorReset} not found in vcpkg, will build from source")
    endif()
endif()

# =============================================================================
# Try system package if not found in vcpkg (skip for musl builds)
# =============================================================================
if(NOT LIBDATACHANNEL_FOUND AND NOT USE_MUSL)
    message(STATUS "${BoldCyan}libdatachannel${ColorReset}: Checking system packages...")

    # Try find_package for system installation (e.g., Arch extra repo)
    find_package(LibDataChannel QUIET)

    if(LibDataChannel_FOUND)
        set(LIBDATACHANNEL_FOUND TRUE)
        message(STATUS "${BoldGreen}libdatachannel${ColorReset} found via system package manager")

        # Create INTERFACE library that wraps the found package
        # LibDataChannel::LibDataChannel is the imported target from find_package
        if(NOT TARGET libdatachannel)
            add_library(libdatachannel INTERFACE)
            target_link_libraries(libdatachannel INTERFACE LibDataChannel::LibDataChannel)
        endif()
    else()
        message(STATUS "${BoldYellow}libdatachannel${ColorReset} not found in system packages")
    endif()
elseif(USE_MUSL)
    message(STATUS "${BoldCyan}libdatachannel${ColorReset}: Skipping system packages (USE_MUSL=ON, will build from source)")
endif()

# =============================================================================
# Build from source if not found anywhere
# =============================================================================
if(NOT LIBDATACHANNEL_FOUND)
    message(STATUS "${BoldCyan}libdatachannel${ColorReset}: Building from source...")

include(FetchContent)

set(LIBDATACHANNEL_REPO "https://github.com/paullouisageneau/libdatachannel")
set(LIBDATACHANNEL_TAG "v0.22.5")  # Latest stable release

message(STATUS "${BoldCyan}libdatachannel WebRTC Library${ColorReset}")
message(STATUS "  Repository: ${LIBDATACHANNEL_REPO}")
message(STATUS "  Version: ${LIBDATACHANNEL_TAG}")

# Fetch libdatachannel source
FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY ${LIBDATACHANNEL_REPO}
    GIT_TAG ${LIBDATACHANNEL_TAG}
    GIT_SHALLOW TRUE
)

cmake_policy(SET CMP0169 OLD)

FetchContent_GetProperties(libdatachannel)
if(NOT libdatachannel_POPULATED)
    # Manually populate the source
    FetchContent_Populate(libdatachannel)

    # Initialize submodules for libdatachannel dependencies
    # Note: libdatachannel requires libsrtp, libjuice, and usrsctp as submodules
    message(STATUS "Initializing libdatachannel submodules...")
    execute_process(
        COMMAND git submodule update --init --recursive --depth 1
        WORKING_DIRECTORY ${libdatachannel_SOURCE_DIR}
        RESULT_VARIABLE SUBMODULE_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT SUBMODULE_RESULT EQUAL 0)
        message(WARNING "Failed to initialize libdatachannel submodules (git may not be available)")
        # Continue anyway - CMake build will fail later if submodules are actually required
    endif()

    # Patch dependency CMakeLists to require modern CMake
    # plog
    set(PLOG_CMAKE_FILE "${libdatachannel_SOURCE_DIR}/deps/plog/CMakeLists.txt")
    if(EXISTS "${PLOG_CMAKE_FILE}")
        file(READ "${PLOG_CMAKE_FILE}" PLOG_CMAKE_CONTENT)
        string(REGEX REPLACE "cmake_minimum_required\\(VERSION [0-9.]+\\)" "cmake_minimum_required(VERSION 3.5)" PLOG_CMAKE_CONTENT "${PLOG_CMAKE_CONTENT}")
        file(WRITE "${PLOG_CMAKE_FILE}" "${PLOG_CMAKE_CONTENT}")
        message(STATUS "Patched plog CMakeLists.txt to require CMake 3.5")
    endif()

    # usrsctp
    set(USRSCTP_CMAKE_FILE "${libdatachannel_SOURCE_DIR}/deps/usrsctp/CMakeLists.txt")
    if(EXISTS "${USRSCTP_CMAKE_FILE}")
        file(READ "${USRSCTP_CMAKE_FILE}" USRSCTP_CMAKE_CONTENT)
        string(REGEX REPLACE "cmake_minimum_required\\(VERSION [0-9.]+\\)" "cmake_minimum_required(VERSION 3.5)" USRSCTP_CMAKE_CONTENT "${USRSCTP_CMAKE_CONTENT}")
        file(WRITE "${USRSCTP_CMAKE_FILE}" "${USRSCTP_CMAKE_CONTENT}")
        message(STATUS "Patched usrsctp CMakeLists.txt to require CMake 3.5")
    endif()

    # Set up build directory in deps cache
    # For musl builds, use musl-specific cache directory to avoid conflicts
    if(USE_MUSL AND MUSL_DEPS_DIR_STATIC)
        set(LIBDATACHANNEL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libdatachannel-build")
    else()
        set(LIBDATACHANNEL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libdatachannel-build")
    endif()
    file(MAKE_DIRECTORY "${LIBDATACHANNEL_BUILD_DIR}")

    # Check if cached build exists
    set(LIBDATACHANNEL_NEEDS_REBUILD FALSE)
    if(NOT EXISTS "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}datachannel${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set(LIBDATACHANNEL_NEEDS_REBUILD TRUE)
        message(STATUS "libdatachannel library not found, will build from source")
    endif()

    if(LIBDATACHANNEL_NEEDS_REBUILD)
        message(STATUS "${BoldYellow}libdatachannel${ColorReset} building from source...")

        # Clean old build to ensure fresh compilation
        if(EXISTS "${LIBDATACHANNEL_BUILD_DIR}/lib")
            file(REMOVE_RECURSE "${LIBDATACHANNEL_BUILD_DIR}/lib")
            message(STATUS "Cleaned old libdatachannel build artifacts")
        endif()
        file(MAKE_DIRECTORY "${LIBDATACHANNEL_BUILD_DIR}/lib")

        # Prepare CMake args for libdatachannel build
        set(LIBDATACHANNEL_CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${LIBDATACHANNEL_BUILD_DIR}/lib
            -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${LIBDATACHANNEL_BUILD_DIR}/lib
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${LIBDATACHANNEL_BUILD_DIR}/bin
            # libdatachannel build options
            -DUSE_GNUTLS=OFF                      # Use OpenSSL instead of GnuTLS
            -DUSE_NICE=OFF                        # Use libjuice (submodule) instead of libnice
            -DNO_WEBSOCKET=ON                     # Disable WebSocket support (we only need DataChannels)
            -DNO_MEDIA=ON                         # Disable media transport (we handle our own via ACIP)
            -DNO_EXAMPLES=ON                      # Don't build examples
            -DNO_TESTS=ON                         # Don't build tests
            -DBUILD_SHARED_LIBS=OFF               # Build static library
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON  # Required for linking into shared library
        )

        # For musl builds, add target triple and OpenSSL paths
        if(USE_MUSL)
            # Determine musl target architecture
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(MUSL_ARCH "aarch64")
            elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "armv7")
                set(MUSL_ARCH "armv7")
            else()
                set(MUSL_ARCH "x86_64")
            endif()
            set(LIBDATACHANNEL_MUSL_TARGET "${MUSL_ARCH}-linux-musl")

            # Set compiler flags for musl
            # Use Alpine's musl-native libc++ headers to ensure ABI compatibility
            # -nostdinc++ removes system C++ headers, -isystem adds Alpine's headers
            # Also need musl C library headers for bits/alltypes.h etc.
            # This prevents glibc-specific symbols like pthread_cond_clockwait
            # MUSL_INCLUDE_DIR is set and cached by Musl.cmake (distro-specific path)
            set(_libdc_c_flags "-O3 -target ${LIBDATACHANNEL_MUSL_TARGET}")
            if(ALPINE_LIBCXX_INCLUDE_DIR AND EXISTS "${ALPINE_LIBCXX_INCLUDE_DIR}" AND MUSL_INCLUDE_DIR AND EXISTS "${MUSL_INCLUDE_DIR}")
                # Order matters: C++ headers first, then musl C headers
                set(_libdc_cxx_flags "-O3 -target ${LIBDATACHANNEL_MUSL_TARGET} -stdlib=libc++ -nostdinc++ -isystem ${ALPINE_LIBCXX_INCLUDE_DIR} -isystem ${MUSL_INCLUDE_DIR}")
                message(STATUS "libdatachannel using Alpine libc++ headers: ${ALPINE_LIBCXX_INCLUDE_DIR}")
                message(STATUS "libdatachannel using musl C headers: ${MUSL_INCLUDE_DIR}")
            else()
                # Fallback: try to suppress glibc-specific code generation
                set(_libdc_cxx_flags "-O3 -target ${LIBDATACHANNEL_MUSL_TARGET} -stdlib=libc++ -U__GLIBC__")
                message(WARNING "Alpine libc++ or musl headers not found (ALPINE_LIBCXX_INCLUDE_DIR='${ALPINE_LIBCXX_INCLUDE_DIR}', MUSL_INCLUDE_DIR='${MUSL_INCLUDE_DIR}'), using system headers with -U__GLIBC__")
            endif()
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_libdc_c_flags}")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_libdc_cxx_flags}")

            # Pass musl-built OpenSSL paths to libdatachannel
            # OPENSSL_ROOT_DIR, OPENSSL_INCLUDE_DIR, etc. are set by MuslDependencies.cmake
            if(OPENSSL_ROOT_DIR)
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DOPENSSL_INCLUDE_DIR=${OPENSSL_INCLUDE_DIR}")
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DOPENSSL_SSL_LIBRARY=${OPENSSL_SSL_LIBRARY}")
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DOPENSSL_CRYPTO_LIBRARY=${OPENSSL_CRYPTO_LIBRARY}")
                message(STATUS "libdatachannel will use musl-built OpenSSL from ${OPENSSL_ROOT_DIR}")
            endif()

            message(STATUS "libdatachannel will be built for musl target: ${LIBDATACHANNEL_MUSL_TARGET}")
        endif()

        # On macOS with Homebrew LLVM, use Apple's system clang for libdatachannel build
        # PROBLEM: Homebrew LLVM 21.x's libc++ has strict header search path requirements.
        # Its <cctype>, <cmath>, etc. expect to find libc++ C wrapper headers in a specific
        # location relative to the main libc++ include directory. When CMAKE_OSX_SYSROOT
        # is set, the compiler finds SDK headers before libc++ wrappers, causing errors like:
        #   "<cctype> tried including <ctype.h> but didn't find libc++'s <ctype.h> header"
        # SOLUTION: Use Apple's system clang (/usr/bin/clang++) for libdatachannel build.
        # Apple's clang properly handles SDK include paths and has compatible libc++.
        if(APPLE AND CMAKE_CXX_COMPILER MATCHES "clang")
            # Check if we're using Homebrew clang (path contains "homebrew" or "Cellar")
            if(CMAKE_CXX_COMPILER MATCHES "(homebrew|Cellar)")
                # Use Apple's system clang for libdatachannel build
                if(EXISTS "/usr/bin/clang" AND EXISTS "/usr/bin/clang++")
                    list(FILTER LIBDATACHANNEL_CMAKE_ARGS EXCLUDE REGEX "CMAKE_C_COMPILER|CMAKE_CXX_COMPILER")
                    list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_COMPILER=/usr/bin/clang")
                    list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_COMPILER=/usr/bin/clang++")
                    message(STATUS "libdatachannel macOS build: using Apple clang (Homebrew LLVM detected)")
                else()
                    message(WARNING "libdatachannel macOS build: Apple clang not found, using Homebrew clang (may fail)")
                endif()
            else()
                message(STATUS "libdatachannel macOS build: using system clang")
            endif()

            # Suppress debug symbols to avoid linker warnings about duplicate debug map objects
            # libdatachannel's debug symbols have invalid timestamps that cause ld64.lld to warn
            # Strip debug symbols and use -w to suppress compiler/linker warnings
            set(_libdc_c_flags "-w -g0")
            set(_libdc_cxx_flags "-w -g0")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_libdc_c_flags}")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_libdc_cxx_flags}")

            # Pass the macOS SDK sysroot for proper SDK header resolution
            if(CMAKE_OSX_SYSROOT)
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
            else()
                execute_process(
                    COMMAND xcrun --show-sdk-path
                    OUTPUT_VARIABLE _macos_sdk_path
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(_macos_sdk_path)
                    list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_OSX_SYSROOT=${_macos_sdk_path}")
                endif()
            endif()
        endif()

        # On Windows, force Ninja generator and use clang-cl for MSVC-style flag compatibility
        # CRITICAL: usrsctp and other dependencies check WIN32 (not MSVC) and add MSVC-style
        # flags like /W4, /wd4100. Raw Clang doesn't understand these (interprets as file paths).
        # Solution: Use clang-cl which understands both MSVC and Clang flags.
        if(WIN32)
            list(PREPEND LIBDATACHANNEL_CMAKE_ARGS -G Ninja)

            # Find clang-cl in the same directory as clang
            get_filename_component(_clang_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            find_program(CLANG_CL_EXECUTABLE
                NAMES clang-cl
                HINTS "${_clang_dir}"
                NO_DEFAULT_PATH
            )
            if(NOT CLANG_CL_EXECUTABLE)
                find_program(CLANG_CL_EXECUTABLE NAMES clang-cl)
            endif()

            if(CLANG_CL_EXECUTABLE)
                # Use clang-cl for libdatachannel build (understands MSVC flags)
                message(STATUS "libdatachannel Windows build: using clang-cl at ${CLANG_CL_EXECUTABLE}")
                # Override the compiler settings for libdatachannel only
                # Filter out existing compiler args and replace with clang-cl
                list(FILTER LIBDATACHANNEL_CMAKE_ARGS EXCLUDE REGEX "CMAKE_C_COMPILER|CMAKE_CXX_COMPILER")
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_COMPILER=${CLANG_CL_EXECUTABLE}")
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_COMPILER=${CLANG_CL_EXECUTABLE}")
            else()
                message(WARNING "clang-cl not found, libdatachannel build may fail with MSVC-style flags")
            endif()

            # Add WIN32_LEAN_AND_MEAN to prevent winsock conflicts
            # CRITICAL: Add /EHsc to enable C++ exception handling (throw/try)
            # clang-cl disables exceptions by default, but libdatachannel uses them
            set(_libdc_win_c_flags "-DWIN32_LEAN_AND_MEAN")
            set(_libdc_win_cxx_flags "-DWIN32_LEAN_AND_MEAN /EHsc")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_libdc_win_c_flags}")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_libdc_win_cxx_flags}")

            # Pass vcpkg toolchain and OpenSSL paths so libdatachannel can find OpenSSL
            if(CMAKE_TOOLCHAIN_FILE)
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
            endif()
            if(VCPKG_TARGET_TRIPLET)
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
            endif()
            # Also pass explicit OpenSSL paths if available from vcpkg
            if(OPENSSL_ROOT_DIR)
                list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")
            elseif(VCPKG_ROOT AND VCPKG_TARGET_TRIPLET)
                # Try to find OpenSSL in vcpkg installed directory
                set(_vcpkg_openssl_root "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
                if(EXISTS "${_vcpkg_openssl_root}/include/openssl/ssl.h")
                    list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DOPENSSL_ROOT_DIR=${_vcpkg_openssl_root}")
                    message(STATUS "libdatachannel using vcpkg OpenSSL from ${_vcpkg_openssl_root}")
                endif()
            endif()

            message(STATUS "libdatachannel Windows build: forcing Ninja generator")
        endif()

        # Build libdatachannel at configure time (not part of main build)
        execute_process(
            COMMAND ${CMAKE_COMMAND}
                ${LIBDATACHANNEL_CMAKE_ARGS}
                ${libdatachannel_SOURCE_DIR}
            WORKING_DIRECTORY "${LIBDATACHANNEL_BUILD_DIR}"
            RESULT_VARIABLE LIBDATACHANNEL_CONFIG_RESULT
        )

        if(NOT LIBDATACHANNEL_CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure libdatachannel")
        endif()

        # Build it
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build . --config ${CMAKE_BUILD_TYPE} -j${CMAKE_BUILD_PARALLEL_LEVEL}
            WORKING_DIRECTORY "${LIBDATACHANNEL_BUILD_DIR}"
            RESULT_VARIABLE LIBDATACHANNEL_BUILD_RESULT
        )

        if(NOT LIBDATACHANNEL_BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build libdatachannel")
        endif()

        # Strip debug symbols from libdatachannel.a to eliminate linker warnings
        # about duplicate debug map objects with invalid timestamps
        if(APPLE)
            find_program(STRIP_EXECUTABLE strip)
            if(STRIP_EXECUTABLE)
                execute_process(
                    COMMAND ${STRIP_EXECUTABLE} -x "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}datachannel${CMAKE_STATIC_LIBRARY_SUFFIX}"
                    RESULT_VARIABLE STRIP_RESULT
                    OUTPUT_QUIET
                    ERROR_QUIET
                )
                if(STRIP_RESULT EQUAL 0)
                    message(STATUS "Stripped debug symbols from libdatachannel.a")
                endif()
            endif()
        endif()

        message(STATUS "${BoldGreen}libdatachannel${ColorReset} library built and cached successfully")
    else()
        message(STATUS "${BoldGreen}libdatachannel${ColorReset} library found in cache: ${BoldCyan}${LIBDATACHANNEL_BUILD_DIR}/lib${ColorReset}")
    endif()

endif()

    # Import pre-built library as INTERFACE library (source-built version)
    # For musl builds, use musl-specific cache directory to avoid conflicts
    if(USE_MUSL AND MUSL_DEPS_DIR_STATIC)
        set(LIBDATACHANNEL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libdatachannel-build")
    else()
        set(LIBDATACHANNEL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libdatachannel-build")
    endif()

    # libdatachannel builds as libdatachannel.a when BUILD_SHARED_LIBS=OFF
    set(LIBDATACHANNEL_STATIC_LIB "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}datachannel${CMAKE_STATIC_LIBRARY_SUFFIX}")

    # libdatachannel dependencies (built as part of libdatachannel)
    set(LIBJUICE_STATIC_LIB "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}juice${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(LIBUSRSCTP_STATIC_LIB "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}usrsctp${CMAKE_STATIC_LIBRARY_SUFFIX}")

    add_library(libdatachannel INTERFACE)

    # Link static libs from source build
    target_link_libraries(libdatachannel INTERFACE
        "${LIBDATACHANNEL_STATIC_LIB}"
        "${LIBJUICE_STATIC_LIB}"
        "${LIBUSRSCTP_STATIC_LIB}"
    )

    # Platform-specific system libraries
    if(APPLE)
        target_link_libraries(libdatachannel INTERFACE
            "-framework Foundation"
            "-framework Security"
            ${ASCIICHAT_STATIC_LIBCXX_LIBS}
        )
    elseif(WIN32)
        target_link_libraries(libdatachannel INTERFACE ws2_32 iphlpapi bcrypt)
    else()
        target_link_libraries(libdatachannel INTERFACE $<$<NOT:$<BOOL:${USE_MUSL}>>:stdc++> pthread)
    endif()

    # OpenSSL for TURN credentials
    if(NOT TARGET OpenSSL::Crypto)
        find_package(OpenSSL REQUIRED)
    endif()
    target_link_libraries(libdatachannel INTERFACE OpenSSL::SSL OpenSSL::Crypto)

    # Add include path for libdatachannel headers
    target_include_directories(libdatachannel
        INTERFACE
        "${libdatachannel_SOURCE_DIR}/include"
    )

    message(STATUS "  ${BoldGreen}✓ libdatachannel configured successfully${ColorReset}")
    message(STATUS "  Source dir: ${libdatachannel_SOURCE_DIR}")
    message(STATUS "  Library: ${LIBDATACHANNEL_STATIC_LIB}")
endif()  # NOT LIBDATACHANNEL_FOUND

message(STATUS "${BoldGreen}libdatachannel WebRTC ready${ColorReset}")
