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
#   1. CMake fetches libdatachannel repository at configure time
#   2. Initialize submodules (libsrtp, libjuice, usrsctp)
#   3. Build completely in deps cache (NOT in build/)
#   4. Import pre-built libraries as INTERFACE library
#   5. This prevents cluttering the main project with submodule targets
# =============================================================================

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
    set(LIBDATACHANNEL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libdatachannel-build")
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

        # For musl builds, add target triple
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
            set(_libdc_c_flags "-O3 -target ${LIBDATACHANNEL_MUSL_TARGET}")
            set(_libdc_cxx_flags "-O3 -target ${LIBDATACHANNEL_MUSL_TARGET} -stdlib=libc++")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_libdc_c_flags}")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_libdc_cxx_flags}")
            message(STATUS "libdatachannel will be built for musl target: ${LIBDATACHANNEL_MUSL_TARGET}")
        endif()

        # On macOS with Homebrew LLVM, set explicit flags
        if(APPLE AND CMAKE_CXX_COMPILER MATCHES "clang")
            get_filename_component(LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            get_filename_component(LLVM_ROOT "${LLVM_BIN_DIR}/.." ABSOLUTE)

            # Check for libc++ include path
            set(LIBCXX_INCLUDE_DIR "${LLVM_ROOT}/include/c++/v1")
            if(EXISTS "${LIBCXX_INCLUDE_DIR}")
                set(_libcxx_include_flag "-isystem ${LIBCXX_INCLUDE_DIR}")
            else()
                set(_libcxx_include_flag "")
            endif()

            set(_libdc_c_flags "--no-default-config -w")
            set(_libdc_cxx_flags "--no-default-config -stdlib=libc++ ${_libcxx_include_flag} -w")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_libdc_cxx_flags}")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_libdc_c_flags}")
            message(STATUS "libdatachannel macOS build: libc++ include=${LIBCXX_INCLUDE_DIR}")
        endif()

        # On Windows, force Ninja generator
        if(WIN32)
            list(PREPEND LIBDATACHANNEL_CMAKE_ARGS -G Ninja)
            # Add WIN32_LEAN_AND_MEAN to prevent winsock conflicts
            set(_libdc_win_c_flags "-DWIN32_LEAN_AND_MEAN")
            set(_libdc_win_cxx_flags "-DWIN32_LEAN_AND_MEAN")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_C_FLAGS=${_libdc_win_c_flags}")
            list(APPEND LIBDATACHANNEL_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${_libdc_win_cxx_flags}")
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

        message(STATUS "${BoldGreen}libdatachannel${ColorReset} library built and cached successfully")
    else()
        message(STATUS "${BoldGreen}libdatachannel${ColorReset} library found in cache: ${BoldCyan}${LIBDATACHANNEL_BUILD_DIR}/lib${ColorReset}")
    endif()

endif()

# Import pre-built library as INTERFACE library
set(LIBDATACHANNEL_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libdatachannel-build")

# libdatachannel builds as libdatachannel.a when BUILD_SHARED_LIBS=OFF
set(LIBDATACHANNEL_STATIC_LIB "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}datachannel${CMAKE_STATIC_LIBRARY_SUFFIX}")

# libdatachannel dependencies (built as part of libdatachannel)
set(LIBJUICE_STATIC_LIB "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}juice${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(LIBUSRSCTP_STATIC_LIB "${LIBDATACHANNEL_BUILD_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}usrsctp${CMAKE_STATIC_LIBRARY_SUFFIX}")

add_library(libdatachannel INTERFACE)

if(APPLE)
    # macOS: link with necessary frameworks
    target_link_libraries(libdatachannel INTERFACE
        "${LIBDATACHANNEL_STATIC_LIB}"
        "${LIBJUICE_STATIC_LIB}"
        "${LIBUSRSCTP_STATIC_LIB}"
        "-framework Foundation"
        "-framework Security"
        c++
    )
elseif(WIN32)
    # Windows: link with Winsock and other system libraries
    target_link_libraries(libdatachannel INTERFACE
        "${LIBDATACHANNEL_STATIC_LIB}"
        "${LIBJUICE_STATIC_LIB}"
        "${LIBUSRSCTP_STATIC_LIB}"
        ws2_32
        iphlpapi
        bcrypt
    )
else()
    # Linux/Unix: link with pthread and OpenSSL
    find_package(OpenSSL REQUIRED)
    target_link_libraries(libdatachannel INTERFACE
        "${LIBDATACHANNEL_STATIC_LIB}"
        "${LIBJUICE_STATIC_LIB}"
        "${LIBUSRSCTP_STATIC_LIB}"
        OpenSSL::SSL
        OpenSSL::Crypto
        $<$<NOT:$<BOOL:${USE_MUSL}>>:stdc++>
        pthread
    )
endif()

# Add include path for libdatachannel headers
target_include_directories(libdatachannel
    INTERFACE
    "${libdatachannel_SOURCE_DIR}/include"
)

message(STATUS "  ${BoldGreen}✓ libdatachannel configured successfully${ColorReset}")
message(STATUS "  Source dir: ${libdatachannel_SOURCE_DIR}")
message(STATUS "  Library: ${LIBDATACHANNEL_STATIC_LIB}")
message(STATUS "${BoldGreen}libdatachannel WebRTC ready${ColorReset}")
