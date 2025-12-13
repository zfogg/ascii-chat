# =============================================================================
# ascii-chat Library Modules
# =============================================================================
# This file orchestrates the build of all library modules.
#
# Module dependency order:
#   1. util         - No dependencies (utilities)
#   2. data-structures - Depends on: util
#   3. core         - Depends on: util (logging, options, config)
#   4. platform     - Depends on: util, data-structures, core
#   5. crypto       - Depends on: util, platform, network
#   6. network      - Depends on: util, platform, crypto, core
#   7. simd         - Depends on: util, core, video
#   8. video        - Depends on: util, platform, core, simd
#   9. audio        - Depends on: util, platform, data-structures
#  10. panic        - Depends on: util, platform, core (tooling runtime)
#
# Note: Some modules have circular dependencies (core<->network, simd<->video)
# which are resolved at link time since all modules are linked together.
# =============================================================================

# Include module creation helpers
include(${CMAKE_SOURCE_DIR}/cmake/targets/ModuleHelpers.cmake)

# =============================================================================
# Include Subdirectory Modules
# =============================================================================
# Each subdirectory defines its sources and creates its library target

add_subdirectory(util)
add_subdirectory(debug)
add_subdirectory(platform)
add_subdirectory(crypto)
add_subdirectory(network)
add_subdirectory(image2ascii)
add_subdirectory(os)
add_subdirectory(tooling)

# =============================================================================
# Core Module Sources (files in lib/ root)
# =============================================================================
# Core infrastructure that doesn't fit into a specific subdirectory

set(CORE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/asciichat_errno.c
    ${CMAKE_CURRENT_SOURCE_DIR}/logging.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options.c
    ${CMAKE_CURRENT_SOURCE_DIR}/config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/version.c
    ${CMAKE_CURRENT_SOURCE_DIR}/palette.c
    # Third-party TOML parser
    ${CMAKE_SOURCE_DIR}/deps/tomlc17/src/tomlc17.c
)

# Only include lock debugging runtime in non-release builds
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/debug/lock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/debug/memory.c
    )
endif()

create_ascii_chat_module(ascii-chat-core "${CORE_SRCS}")

# Disable precompiled headers and static analyzers for tomlc17 (third-party code)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/tomlc17/src/tomlc17.c
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_LINTING ON
)

# Math library (Unix only - Windows has math functions in C runtime)
if(NOT WIN32)
    target_link_libraries(ascii-chat-core m)
endif()

# Special musl handling for libexecinfo
if(USE_MUSL)
    target_link_libraries(ascii-chat-core ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a)
    add_dependencies(ascii-chat-core libexecinfo-musl)
endif()

# mimalloc for core
if(USE_MIMALLOC)
    if(TARGET mimalloc-static)
        add_dependencies(ascii-chat-core mimalloc-static)
    endif()
    if(ASCIICHAT_MIMALLOC_LINK_LIB)
        target_link_libraries(ascii-chat-core ${ASCIICHAT_MIMALLOC_LINK_LIB})
    elseif(MIMALLOC_LIBRARIES)
        target_link_libraries(ascii-chat-core ${MIMALLOC_LIBRARIES})
    endif()
endif()

# =============================================================================
# Data Structures Module
# =============================================================================
set(DATA_STRUCTURES_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/ringbuffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/packet_queue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_pool.c
)

create_ascii_chat_module(ascii-chat-data-structures "${DATA_STRUCTURES_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-data-structures ascii-chat-util)
endif()

# =============================================================================
# Audio Module
# =============================================================================
set(AUDIO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mixer.c
)

create_ascii_chat_module(ascii-chat-audio "${AUDIO_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-audio
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-data-structures
        ${PORTAUDIO_LIBRARIES}
    )
else()
    # For OBJECT libs, link external deps only
    target_link_libraries(ascii-chat-audio ${PORTAUDIO_LIBRARIES})
endif()

# Link platform-specific audio libraries
if(APPLE)
    target_link_libraries(ascii-chat-audio
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOUNIT_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${CORESERVICES_FRAMEWORK}
    )
elseif(UNIX AND NOT USE_MUSL)
    # Check if JACK is available
    set(JACK_FOUND FALSE)
    set(JACK_LIBRARY JACK_LIBRARY-NOTFOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(JACK QUIET jack)
        if(JACK_FOUND)
            set(JACK_LIBRARY ${JACK_LIBRARIES})
            target_link_libraries(ascii-chat-audio ${JACK_LIBRARIES})
            message(STATUS "Found ${BoldBlue}JACK${ColorReset} via pkg-config: ${BoldGreen}${JACK_LIBRARIES}${ColorReset}")
        endif()
    endif()

    if(NOT JACK_FOUND)
        find_library(JACK_LIBRARY NAMES jack)
        if(JACK_LIBRARY)
            target_link_libraries(ascii-chat-audio ${JACK_LIBRARY})
            message(STATUS "Found ${BoldBlue}JACK${ColorReset}: ${BoldGreen}${JACK_LIBRARY}${ColorReset}")
        else()
            message(STATUS "${BoldYellow}JACK not found${ColorReset} - PortAudio will use ALSA/OSS only")
        endif()
    endif()
endif()

# =============================================================================
# Video Frame Module
# =============================================================================
set(VIDEO_FRAME_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/video_frame.c
)

# Video frame is part of ascii-chat-video module (defined in image2ascii/)

# =============================================================================
# Module Dependencies (non-OBJECT builds)
# =============================================================================
# Set up inter-module dependencies for static library builds
# OBJECT builds defer linking to the final DLL target

if(NOT BUILDING_OBJECT_LIBS)
    # Core depends on util and platform
    target_link_libraries(ascii-chat-core
        ascii-chat-util
        ascii-chat-platform
    )

    # Platform depends on util, data-structures, core
    target_link_libraries(ascii-chat-platform
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-core
    )
endif()

# =============================================================================
# Library Alias for Tests
# =============================================================================
# Create an alias that links all modules for test compatibility
# Note: Libraries with circular dependencies must be listed in correct order
add_library(ascii-chat-lib INTERFACE)
target_link_libraries(ascii-chat-lib INTERFACE
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-core
    ascii-chat-network
    ascii-chat-crypto
    ascii-chat-network
    ascii-chat-core
    ascii-chat-platform
    ascii-chat-data-structures
    ascii-chat-util
)

# =============================================================================
# Unified Library Targets
# =============================================================================
# Include the unified library configuration (static and shared libraries)
include(${CMAKE_SOURCE_DIR}/cmake/targets/UnifiedLibraries.cmake)
