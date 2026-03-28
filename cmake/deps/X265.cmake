# =============================================================================
# x265 HEVC Encoder Library Configuration
# =============================================================================
# Finds and configures x265 for HEVC encoding support in FFmpeg
#
# Platform-specific dependency management:
#   - musl: Built from source with musl-gcc
#   - Linux/macOS (non-musl): Uses pkg-config for system packages
#   - Windows: Uses vcpkg
#
# Outputs:
#   - X265_FOUND - Whether x265 was found/configured
#   - X265_LIBRARIES - Library paths
#   - X265_INCLUDE_DIRS - Include directories
#   - X265_PREFIX - Install prefix (for musl builds, passed to FFmpeg configure)
# =============================================================================

set(X265_VERSION "4.1")
set(X265_HASH "7d23cdcdbd510728202c0dfbf7c51eda26a395de2096c504c2b10d6035711102")

# =============================================================================
# Windows: x265 not needed (vcpkg FFmpeg is built without libx265)
# =============================================================================
if(WIN32)
    set(X265_FOUND FALSE)
    message(STATUS "⚠ x265 not needed on Windows (vcpkg FFmpeg uses built-in codecs)")
    return()
endif()

# =============================================================================
# musl: Build from source
# =============================================================================
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}x265${ColorReset} from source (musl)...")

    set(X265_PREFIX "${MUSL_DEPS_DIR_STATIC}/x265")
    set(X265_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/x265-build")
    set(X265_SOURCE_DIR "${X265_BUILD_DIR}/src")

    if(NOT EXISTS "${X265_PREFIX}/lib/libx265.a" OR NOT EXISTS "${X265_PREFIX}/lib/pkgconfig/x265.pc")
        message(STATUS "  x265 library not found in cache, will build from source")

        file(MAKE_DIRECTORY "${X265_BUILD_DIR}")
        file(MAKE_DIRECTORY "${X265_SOURCE_DIR}")

        # Download x265 source
        set(X265_TARBALL "${X265_BUILD_DIR}/x265-${X265_VERSION}.tar.gz")
        if(NOT EXISTS "${X265_TARBALL}")
            message(STATUS "  Downloading x265 ${X265_VERSION}...")
            file(DOWNLOAD
                "https://bitbucket.org/multicoreware/x265_git/get/${X265_VERSION}.tar.gz"
                "${X265_TARBALL}"
                EXPECTED_HASH SHA256=${X265_HASH}
                STATUS DOWNLOAD_STATUS
                SHOW_PROGRESS
            )
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                list(GET DOWNLOAD_STATUS 1 ERROR_MSG)
                message(FATAL_ERROR "Failed to download x265: ${ERROR_MSG}")
            endif()
        endif()

        # Extract tarball
        if(NOT EXISTS "${X265_SOURCE_DIR}/source/CMakeLists.txt")
            message(STATUS "  Extracting x265...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${X265_TARBALL}"
                WORKING_DIRECTORY "${X265_BUILD_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            if(NOT EXTRACT_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to extract x265 tarball")
            endif()
            # Bitbucket tarballs extract to multicoreware-x265_git-<hash>/
            file(GLOB X265_EXTRACTED_DIR "${X265_BUILD_DIR}/multicoreware-x265_git-*")
            if(X265_EXTRACTED_DIR)
                file(RENAME "${X265_EXTRACTED_DIR}" "${X265_SOURCE_DIR}")
            endif()
        endif()

        # Patch x265 CMakeLists.txt for modern CMake compatibility
        # x265 tries to set CMP0054 to OLD which is no longer supported
        set(X265_CMAKELISTS "${X265_SOURCE_DIR}/source/CMakeLists.txt")
        file(READ "${X265_CMAKELISTS}" X265_CMAKE_CONTENT)
        string(REPLACE "cmake_policy(SET CMP0054 OLD)" "cmake_policy(SET CMP0054 NEW)" X265_CMAKE_CONTENT "${X265_CMAKE_CONTENT}")
        string(REPLACE "cmake_policy(SET CMP0025 OLD)" "cmake_policy(SET CMP0025 NEW)" X265_CMAKE_CONTENT "${X265_CMAKE_CONTENT}")
        # x265 forces -std=gnu++98 via add_definitions which breaks musl/libc++ compatibility
        string(REPLACE "add_definitions(-std=gnu++98)" "# patched: C++ standard set via CMAKE_CXX_STANDARD" X265_CMAKE_CONTENT "${X265_CMAKE_CONTENT}")
        string(REPLACE "add_definitions(-std=gnu++11)" "# patched: C++ standard set via CMAKE_CXX_STANDARD" X265_CMAKE_CONTENT "${X265_CMAKE_CONTENT}")
        string(REGEX REPLACE "cmake_minimum_required *\\(VERSION [0-9.]+" "cmake_minimum_required(VERSION 3.5" X265_CMAKE_CONTENT "${X265_CMAKE_CONTENT}")
        file(WRITE "${X265_CMAKELISTS}" "${X265_CMAKE_CONTENT}")

        # Build x265 with CMake using musl
        message(STATUS "  Configuring x265...")

        include(ProcessorCount)
        ProcessorCount(NPROC)
        if(NPROC EQUAL 0)
            set(NPROC 4)
        endif()

        file(MAKE_DIRECTORY "${X265_BUILD_DIR}/cmake-build")

        # x265 is C++ — use clang with musl target triple matching WebRTC AEC3 pattern
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_X265_MUSL_TARGET "aarch64-linux-musl")
        else()
            set(_X265_MUSL_TARGET "x86_64-linux-musl")
        endif()
        set(_X265_MUSL_SYSINCLUDE "-isystem ${MUSL_INCLUDE_DIR}")
        set(_X265_C_FLAGS "-fPIC -target ${_X265_MUSL_TARGET} ${_X265_MUSL_SYSINCLUDE}")
        # libc++ include must come BEFORE musl include — libc++ provides wrapper
        # headers (stddef.h, stdint.h, etc.) that #include_next the C versions
        set(_X265_CXX_FLAGS "-fPIC -target ${_X265_MUSL_TARGET} -stdlib=libc++ -nostdinc++ -D_GNU_SOURCE")
        if(ALPINE_LIBCXX_INCLUDE_DIR)
            set(_X265_CXX_FLAGS "${_X265_CXX_FLAGS} -isystem ${ALPINE_LIBCXX_INCLUDE_DIR}")
        endif()
        set(_X265_CXX_FLAGS "${_X265_CXX_FLAGS} ${_X265_MUSL_SYSINCLUDE}")

        execute_process(
            COMMAND ${CMAKE_COMMAND}
                -S "${X265_SOURCE_DIR}/source"
                -B "${X265_BUILD_DIR}/cmake-build"
                -G Ninja
                -DCMAKE_INSTALL_PREFIX=${X265_PREFIX}
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_C_COMPILER=clang
                -DCMAKE_CXX_COMPILER=clang++
                "-DCMAKE_C_FLAGS=${_X265_C_FLAGS}"
                "-DCMAKE_CXX_FLAGS=${_X265_CXX_FLAGS}"
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                -DCMAKE_CXX_STANDARD=14
                -DENABLE_SHARED=OFF
                -DENABLE_CLI=OFF
                -DENABLE_ASSEMBLY=OFF
                -DENABLE_LIBNUMA=OFF
            WORKING_DIRECTORY "${X265_BUILD_DIR}/cmake-build"
            RESULT_VARIABLE CONFIG_RESULT
            OUTPUT_VARIABLE CONFIG_OUTPUT
            ERROR_VARIABLE CONFIG_ERROR
        )
        if(NOT CONFIG_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to configure x265:\n${CONFIG_OUTPUT}\n${CONFIG_ERROR}")
        endif()

        message(STATUS "  Building x265...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build "${X265_BUILD_DIR}/cmake-build" -j ${NPROC}
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )
        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build x265:\n${BUILD_OUTPUT}\n${BUILD_ERROR}")
        endif()

        message(STATUS "  Installing x265...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} --install "${X265_BUILD_DIR}/cmake-build"
            RESULT_VARIABLE INSTALL_RESULT
            OUTPUT_VARIABLE INSTALL_OUTPUT
            ERROR_VARIABLE INSTALL_ERROR
        )
        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to install x265:\n${INSTALL_ERROR}")
        endif()

        message(STATUS "  ${BoldGreen}x265${ColorReset} built and installed successfully")
    else()
        message(STATUS "  ${BoldGreen}x265${ColorReset} library found in cache: ${BoldMagenta}${X265_PREFIX}/lib/libx265.a${ColorReset}")
    endif()

    # x265 built with clang -stdlib=libc++ needs libc++ symbols at link time.
    # Rewrite x265.pc to include the alpine libc++ .a files directly in Libs
    # so FFmpeg's pkg-config link test can resolve C++ symbols. Clear
    # Libs.private to avoid musl-gcc trying to find -lc++ by name.
    # Note: ALPINE_LIBCXX_DIR is function-scoped in Musl.cmake, so derive it here.
    # x265 built with clang -stdlib=libc++ needs libc++/libc++abi/libunwind
    # at link time. Rewrite x265.pc every configure to ensure correct paths
    # (CI caches may have stale paths or missing libraries).
    set(_alpine_libcxx_dir "${ASCIICHAT_DEPS_CACHE_DIR}/alpine-libcxx")
    set(_x265_pc "${X265_PREFIX}/lib/pkgconfig/x265.pc")
    if(EXISTS "${_x265_pc}" AND EXISTS "${_alpine_libcxx_dir}/usr/lib/libc++.a")
        set(_x265_pc_libs "-L\${libdir} -lx265 -L${_alpine_libcxx_dir}/usr/lib -l:libc++.a -l:libc++abi.a")
        if(EXISTS "${_alpine_libcxx_dir}/usr/lib/libunwind.a")
            set(_x265_pc_libs "${_x265_pc_libs} -l:libunwind.a")
        endif()
        file(READ "${_x265_pc}" _x265_pc_contents)
        string(REGEX REPLACE "Libs:[^\n]*" "Libs: ${_x265_pc_libs}" _x265_pc_contents "${_x265_pc_contents}")
        string(REGEX REPLACE "Libs\\.private:[^\n]*" "Libs.private:" _x265_pc_contents "${_x265_pc_contents}")
        file(WRITE "${_x265_pc}" "${_x265_pc_contents}")
        message(STATUS "  Patched x265.pc with alpine libc++ link flags")
    endif()

    set(X265_LIBRARIES "${X265_PREFIX}/lib/libx265.a")
    set(X265_INCLUDE_DIRS "${X265_PREFIX}/include")

    file(MAKE_DIRECTORY "${X265_PREFIX}/include" "${X265_PREFIX}/lib")

    if(NOT TARGET x265::x265)
        add_library(x265::x265 STATIC IMPORTED GLOBAL)
        set_target_properties(x265::x265 PROPERTIES
            IMPORTED_LOCATION "${X265_PREFIX}/lib/libx265.a"
            INTERFACE_INCLUDE_DIRECTORIES "${X265_PREFIX}/include"
        )
    endif()

    set(X265_FOUND TRUE)
    message(STATUS "${BoldGreen}✓${ColorReset} x265 (musl): ${X265_PREFIX}/lib/libx265.a")
    return()
endif()

# =============================================================================
# Non-musl: Use system package via pkg-config
# =============================================================================
include(FindPkgConfig)
pkg_check_modules(X265 REQUIRED x265)

if(NOT TARGET x265::x265)
    add_library(x265::x265 INTERFACE IMPORTED)
    target_include_directories(x265::x265 SYSTEM INTERFACE ${X265_INCLUDE_DIRS})
    target_link_libraries(x265::x265 INTERFACE ${X265_LIBRARIES})
endif()

set(X265_FOUND TRUE)
message(STATUS "${BoldGreen}✓${ColorReset} x265 found")
