# =============================================================================
# ALSA (Advanced Linux Sound Architecture) Dependency
# =============================================================================
# Cross-platform configuration for ALSA sound support
#
# For musl builds: Built from source
# For native builds: Uses system package manager
#
# Output:
#   - asciichat-alsa: canonical interface target
# =============================================================================

# Musl build: Build from source in MuslDependencies.cmake
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}ALSA${ColorReset} from source (musl)...")

    include(ExternalProject)

    # Keep the corrected musl build isolated from archives produced before
    # header isolation was added; the old installed libasound.a must not
    # satisfy the cache check below.
    set(ALSA_PREFIX "${MUSL_DEPS_DIR_STATIC}/alsa-lib-musl-v2")
    # Bump the build directory when configure flags change so stale glibc
    # configure results cannot be reused by a musl release build.
    set(ALSA_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/alsa-lib-build-musl-v2")

    # Only add external project if library doesn't exist
    if(NOT EXISTS "${ALSA_PREFIX}/lib/libasound.a")
        message(STATUS "  ALSA library not found in cache, will build from source")
        string(REPLACE ";" " " _alsa_standard_libraries "${CMAKE_C_STANDARD_LIBRARIES}")
        string(REPLACE "\"" "" _alsa_standard_libraries "${_alsa_standard_libraries}")
        set(_alsa_ldflags "${CMAKE_EXE_LINKER_FLAGS} ${_alsa_standard_libraries}")
        ExternalProject_Add(asciichat-alsa-musl
            URL https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.12.tar.bz2
            URL_HASH SHA256=4868cd908627279da5a634f468701625be8cc251d84262c7e5b6a218391ad0d2
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${ALSA_BUILD_DIR}
            STAMP_DIR ${ALSA_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                "CC=${ASCIICHAT_CLANG_EXECUTABLE}"
                "CFLAGS=-nostdlibinc -isystem ${MUSL_INCLUDE_DIR} ${MUSL_KERNEL_CFLAGS} ${ASCIICHAT_MUSL_TARGET_FLAG}"
                "LDFLAGS=${_alsa_ldflags}"
                <SOURCE_DIR>/configure --host=${MUSL_TARGET} --prefix=${ALSA_PREFIX} --enable-static --disable-shared --disable-maintainer-mode
            BUILD_COMMAND ${CMAKE_COMMAND} -E env
                "CC=${ASCIICHAT_CLANG_EXECUTABLE}"
                "CFLAGS=-nostdlibinc -isystem ${MUSL_INCLUDE_DIR} ${MUSL_KERNEL_CFLAGS} ${ASCIICHAT_MUSL_TARGET_FLAG}"
                "LDFLAGS=${_alsa_ldflags}"
                make -j
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${ALSA_PREFIX}/lib/libasound.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}ALSA${ColorReset} library found in cache: ${BoldMagenta}${ALSA_PREFIX}/lib/libasound.a${ColorReset}")
        # Create a dummy target so dependencies can reference it
        add_custom_target(asciichat-alsa-musl)
    endif()

    # Set ALSA variables for PortAudio to find
    set(_asciichat_alsa_libraries "${ALSA_PREFIX}/lib/libasound.a")
    set(_asciichat_alsa_include_dirs "${ALSA_PREFIX}/include")
    if(NOT TARGET asciichat-alsa)
        add_library(asciichat-alsa INTERFACE)
        target_link_libraries(asciichat-alsa INTERFACE "${ALSA_PREFIX}/lib/libasound.a")
        target_include_directories(asciichat-alsa INTERFACE "${ALSA_PREFIX}/include")
    endif()

    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_asciichat_alsa alsa REQUIRED)

    if(NOT TARGET asciichat-alsa)
        add_library(asciichat-alsa INTERFACE)
        target_link_libraries(asciichat-alsa INTERFACE ${_asciichat_alsa_LDFLAGS})
        target_include_directories(asciichat-alsa INTERFACE ${_asciichat_alsa_INCLUDE_DIRS})
    endif()

    message(STATUS "${BoldGreen}✓${ColorReset} ALSA: ${_asciichat_alsa_LDFLAGS}")

elseif(APPLE OR EMSCRIPTEN)
    # ALSA is Linux-only and is not used by the browser audio path.
    message(STATUS "${BoldYellow}⚠${ColorReset} ALSA is Linux-only, skipping for this platform")

elseif(WIN32)
    message(STATUS "${BoldYellow}⚠${ColorReset} ALSA is Linux/macOS only, skipping for Windows")

else()
    message(FATAL_ERROR "Unsupported platform for ALSA")
endif()
