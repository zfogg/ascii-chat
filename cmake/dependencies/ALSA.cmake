# =============================================================================
# ALSA (Advanced Linux Sound Architecture) Dependency
# =============================================================================
# Cross-platform configuration for ALSA sound support
#
# For musl builds: Built from source
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - ALSA_LIBRARIES: ALSA libraries to link (PARENT_SCOPE)
#   - ALSA_INCLUDE_DIRS: ALSA include directories (PARENT_SCOPE)
# =============================================================================

# Musl build: Build from source in MuslDependencies.cmake
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}ALSA${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(ALSA_PREFIX "${MUSL_DEPS_DIR_STATIC}/alsa-lib")
    set(ALSA_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/alsa-lib-build")

    # Only add external project if library doesn't exist
    if(NOT EXISTS "${ALSA_PREFIX}/lib/libasound.a")
        message(STATUS "  ALSA library not found in cache, will build from source")
        ExternalProject_Add(alsa-lib-musl
            URL https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.12.tar.bz2
            URL_HASH SHA256=4868cd908627279da5a634f468701625be8cc251d84262c7e5b6a218391ad0d2
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${ALSA_BUILD_DIR}
            STAMP_DIR ${ALSA_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CONFIGURE_COMMAND env CC=${MUSL_GCC} REALGCC=${REAL_GCC} CFLAGS=${MUSL_KERNEL_CFLAGS} <SOURCE_DIR>/configure --host=x86_64-linux-gnu --prefix=${ALSA_PREFIX} --enable-static --disable-shared --disable-maintainer-mode
            BUILD_COMMAND env REALGCC=${REAL_GCC} CFLAGS=-fPIC make -j
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
        add_custom_target(alsa-lib-musl)
    endif()

    # Set ALSA variables for PortAudio to find
    set(ALSA_LIBRARIES "${ALSA_PREFIX}/lib/libasound.a" PARENT_SCOPE)
    set(ALSA_INCLUDE_DIRS "${ALSA_PREFIX}/include" PARENT_SCOPE)

    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA alsa REQUIRED)

    set(ALSA_LIBRARIES ${ALSA_LDFLAGS} PARENT_SCOPE)
    set(ALSA_INCLUDE_DIRS ${ALSA_INCLUDE_DIRS} PARENT_SCOPE)

    message(STATUS "${BoldGreen}✓${ColorReset} ALSA: ${ALSA_LDFLAGS}")

elseif(APPLE)
    # macOS: ALSA is Linux-only, not available on macOS
    message(STATUS "${BoldYellow}⚠${ColorReset} ALSA is Linux-only, skipping for macOS")
    set(ALSA_LIBRARIES "" PARENT_SCOPE)
    set(ALSA_INCLUDE_DIRS "" PARENT_SCOPE)

elseif(WIN32)
    message(STATUS "${BoldYellow}⚠${ColorReset} ALSA is Linux/macOS only, skipping for Windows")
    set(ALSA_LIBRARIES "" PARENT_SCOPE)
    set(ALSA_INCLUDE_DIRS "" PARENT_SCOPE)

else()
    message(FATAL_ERROR "Unsupported platform for ALSA")
endif()
