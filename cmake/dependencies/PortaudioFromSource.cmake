# =============================================================================
# PortAudio Build From Source (No JACK Support)
# =============================================================================
# Builds PortAudio from source with JACK explicitly disabled to avoid
# transitive Berkeley DB dependency from libjack.
#
# This configuration is used for all platforms to ensure consistent behavior
# and eliminate the ~1.6MB unused Berkeley DB dependency.
#
# Supported backends by platform:
#   - Linux: ALSA only (no JACK, no OSS)
#   - macOS: CoreAudio only
#   - Windows: DirectSound, WASAPI (via vcpkg, not built here)
# =============================================================================

include(ExternalProject)

# Platform-specific configuration
if(UNIX AND NOT APPLE)
    # Linux: Build with ALSA only
    set(PORTAUDIO_CONFIGURE_FLAGS
        --enable-static
        --disable-shared
        --with-alsa
        --without-jack
        --without-oss
        --without-pulseaudio  # Use ALSA directly, PipeWire/PulseAudio work via ALSA compatibility
    )
    set(PORTAUDIO_BACKEND "ALSA")
elseif(APPLE)
    # macOS: Build with CoreAudio only
    set(PORTAUDIO_CONFIGURE_FLAGS
        --enable-static
        --disable-shared
        --with-coreaudio
        --without-jack
    )
    set(PORTAUDIO_BACKEND "CoreAudio")
else()
    message(FATAL_ERROR "PortAudio from source build not supported on this platform (use vcpkg for Windows)")
endif()

# Dependency cache directory
set(PORTAUDIO_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/portaudio")
set(PORTAUDIO_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/portaudio-build")

# Only build if library doesn't exist
if(NOT EXISTS "${PORTAUDIO_PREFIX}/lib/libportaudio.a")
    message(STATUS "Building ${BoldBlue}PortAudio${ColorReset} from source (${PORTAUDIO_BACKEND} backend, no JACK)")

    # On Linux, ensure ALSA development headers are available
    if(UNIX AND NOT APPLE)
        find_package(ALSA QUIET)
        if(NOT ALSA_FOUND)
            message(WARNING "ALSA development headers not found. Install libasound2-dev (Debian/Ubuntu) or alsa-lib-devel (Fedora/RHEL)")
        endif()
    endif()

    ExternalProject_Add(portaudio-build
        URL http://files.portaudio.com/archives/pa_stable_v190700_20210406.tgz
        URL_HASH SHA256=47efbf42c77c19a05d22e627d42873e991ec0c1357219c0d74ce6a2948cb2def
        TLS_VERIFY FALSE  # PortAudio's SSL cert is expired (Dec 2025)
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX ${PORTAUDIO_BUILD_DIR}
        STAMP_DIR ${PORTAUDIO_BUILD_DIR}/stamps
        UPDATE_DISCONNECTED 1
        BUILD_ALWAYS 0
        CONFIGURE_COMMAND env CFLAGS=-fPIC <SOURCE_DIR>/configure --prefix=${PORTAUDIO_PREFIX} ${PORTAUDIO_CONFIGURE_FLAGS}
        BUILD_COMMAND make -j${CMAKE_BUILD_PARALLEL_LEVEL}
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${PORTAUDIO_PREFIX}/lib/libportaudio.a
        LOG_DOWNLOAD TRUE
        LOG_CONFIGURE TRUE
        LOG_BUILD TRUE
        LOG_INSTALL TRUE
        LOG_OUTPUT_ON_FAILURE TRUE
    )
else()
    message(STATUS "${BoldBlue}PortAudio${ColorReset} library found in cache: ${BoldMagenta}${PORTAUDIO_PREFIX}/lib/libportaudio.a${ColorReset}")
    add_custom_target(portaudio-build)
endif()

# Set output variables for ascii-chat to link against
set(PORTAUDIO_FOUND TRUE)
set(PORTAUDIO_LIBRARIES "${PORTAUDIO_PREFIX}/lib/libportaudio.a")
set(PORTAUDIO_INCLUDE_DIRS "${PORTAUDIO_PREFIX}/include")

# Export target name for dependency tracking
set(PORTAUDIO_BUILD_TARGET portaudio-build)

message(STATUS "${BoldGreen}✓${ColorReset} PortAudio configured: ${PORTAUDIO_BACKEND} backend (JACK disabled)")
