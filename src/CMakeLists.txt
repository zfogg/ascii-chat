# =============================================================================
# Main Executable (ascii-chat)
# =============================================================================
# Creates the unified ascii-chat binary with both server and client modes.
#
# Prerequisites:
#   - All libraries must be created (via lib/CMakeLists.txt)
#   - Platform detection complete
#   - USE_MIMALLOC, USE_MUSL known
#
# Outputs:
#   - ascii-chat executable target
# =============================================================================

# =============================================================================
# Application Sources
# =============================================================================
set(APP_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    # Server mode sources
    ${CMAKE_CURRENT_SOURCE_DIR}/server/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/stream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/render.c
    ${CMAKE_CURRENT_SOURCE_DIR}/server/stats.c
    # Client mode sources
    ${CMAKE_CURRENT_SOURCE_DIR}/client/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/mirror.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/crypto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/capture.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client/keepalive.c
)

# =============================================================================
# Create Executable
# =============================================================================
add_executable(ascii-chat ${APP_SRCS})

if(ASCIICHAT_ENABLE_UNITY_BUILDS)
    set_target_properties(ascii-chat PROPERTIES UNITY_BUILD ON)
endif()

if(ASCIICHAT_ENABLE_IPO)
    set_property(TARGET ascii-chat PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# =============================================================================
# Library Linking
# =============================================================================
# Debug/Dev: shared library (DLL on Windows) - except musl which needs static
# Release: static library
# USE_MUSL: always static (musl requires static linking)

if((CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev") AND NOT USE_MUSL)
    add_dependencies(ascii-chat ascii-chat-shared generate_version)
    target_link_libraries(ascii-chat ascii-chat-shared)

    # Explicitly add system libraries when using shared library
    if(WIN32)
        target_link_directories(ascii-chat PRIVATE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
        target_link_libraries(ascii-chat
            ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
            ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
            crypt32
            Winmm
            ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        endif()
    else()
        target_link_libraries(ascii-chat
            ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat ${BEARSSL_LIBRARIES})
        endif()
        if(USE_MIMALLOC AND MIMALLOC_LIBRARIES)
            target_link_libraries(ascii-chat ${MIMALLOC_LIBRARIES})
        endif()
    endif()
else()
    # Release builds OR USE_MUSL builds: use static library
    add_dependencies(ascii-chat ascii-chat-static-build generate_version)
    target_link_libraries(ascii-chat ascii-chat-static)
    if(WIN32)
        target_compile_definitions(ascii-chat PRIVATE
            BUILDING_STATIC_LIB=1
            _WIN32_WINNT=0x0A00
        )
    endif()
endif()

# =============================================================================
# RPATH Configuration
# =============================================================================
if((APPLE OR UNIX) AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(ascii-chat PROPERTIES
        BUILD_RPATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY};${CMAKE_BUILD_RPATH}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(ascii-chat PROPERTIES
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH ""
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# =============================================================================
# Include Directories
# =============================================================================
target_include_directories(ascii-chat PRIVATE $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>)

if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
    target_include_directories(ascii-chat PRIVATE ${MIMALLOC_INCLUDE_DIRS})
endif()

# =============================================================================
# Build Timing
# =============================================================================
add_custom_command(TARGET ascii-chat PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Starting ascii-chat timer"
    VERBATIM
)
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Finishing ascii-chat timer"
    VERBATIM
)

# Custom target wrapper for ALL builds
add_custom_target(show-ascii-chat-success ALL
    COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=ascii-chat -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMAND_ECHO NONE
    DEPENDS ascii-chat
    VERBATIM
)

# =============================================================================
# macOS Info.plist Embedding
# =============================================================================
if(PLATFORM_DARWIN AND EXISTS "${CMAKE_SOURCE_DIR}/Info.plist")
    set_target_properties(ascii-chat PROPERTIES
        LINK_FLAGS "-sectcreate __TEXT __info_plist ${CMAKE_SOURCE_DIR}/Info.plist"
    )
endif()

# =============================================================================
# Musl Static Build Configuration
# =============================================================================
if(USE_MUSL)
    add_dependencies(ascii-chat portaudio-musl alsa-lib-musl libsodium-musl zstd-musl libexecinfo-musl)

    target_link_directories(ascii-chat PRIVATE
        ${PORTAUDIO_PREFIX}/lib
        ${ALSA_PREFIX}/lib
        ${LIBSODIUM_PREFIX}/lib
        ${LIBEXECINFO_PREFIX}/lib
    )
    target_link_options(ascii-chat PRIVATE -static -fuse-ld=lld)

    target_link_libraries(ascii-chat
        ${PORTAUDIO_PREFIX}/lib/libportaudio.a
        ${ALSA_PREFIX}/lib/libasound.a
        ${LIBSODIUM_PREFIX}/lib/libsodium.a
        ${LIBEXECINFO_PREFIX}/lib/libexecinfo.a
        -lm -lpthread
    )

    set_target_properties(ascii-chat PROPERTIES
        SKIP_BUILD_RPATH TRUE
        INSTALL_RPATH ""
        INSTALL_RPATH_USE_LINK_PATH FALSE
    )
endif()

# =============================================================================
# PIE (Position Independent Executable)
# =============================================================================
include(CheckPIESupported)
check_pie_supported(OUTPUT_VARIABLE PIE_OUTPUT LANGUAGES C)

if(NOT WIN32 AND CMAKE_C_LINK_PIE_SUPPORTED)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        if(APPLE)
            target_link_options(ascii-chat PRIVATE -Wl,-pie)
        elseif(PLATFORM_LINUX)
            target_link_options(ascii-chat PRIVATE "LINKER:-pie")
        endif()
    endif()
endif()

# =============================================================================
# Dead Code Elimination
# =============================================================================
target_compile_options(ascii-chat PRIVATE -ffunction-sections -fdata-sections)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT WIN32)
        target_compile_options(ascii-chat PRIVATE -fvisibility=hidden)
    endif()
endif()

if(USE_MIMALLOC)
    message(STATUS "ascii-chat linking mimalloc: ${ASCIICHAT_MIMALLOC_LINK_LIB}")
    message(STATUS "ascii-chat MIMALLOC_LIBRARIES raw: ${MIMALLOC_LIBRARIES}")
    if(TARGET mimalloc-static)
        add_dependencies(ascii-chat mimalloc-static)
    endif()
    if(TARGET mimalloc-shared)
        add_dependencies(ascii-chat mimalloc-shared)
    endif()
endif()

if(ASCIICHAT_LLVM_STATIC_LIBUNWIND)
    target_link_libraries(ascii-chat ${ASCIICHAT_LLVM_STATIC_LIBUNWIND})
endif()

if(NOT WIN32)
    if(APPLE)
        target_link_options(ascii-chat PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(ascii-chat PRIVATE -Wl,--gc-sections)
    endif()
endif()

# =============================================================================
# macOS Code Signing
# =============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/platform/CodeSigning.cmake)

# =============================================================================
# Global Build Timer - End Marker
# =============================================================================
add_custom_command(TARGET ascii-chat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Finishing total build timer"
    VERBATIM
)

add_custom_target(build-timer-end ALL
    DEPENDS show-ascii-chat-success
    VERBATIM
)

# =============================================================================
# Build-All Target
# =============================================================================
set(BUILD_ALL_DEPS ascii-chat ascii-chat-shared)
if(NOT BUILDING_OBJECT_LIBS)
    list(APPEND BUILD_ALL_DEPS static-lib)
endif()

add_custom_target(build-all
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMAND_ECHO NONE
    DEPENDS ${BUILD_ALL_DEPS}
    COMMENT "Finishing build-all timer"
    VERBATIM
)

# =============================================================================
# Release Binary Validation (Linux ELF only)
# =============================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND UNIX AND NOT APPLE)
    if(ASCIICHAT_LLVM_READELF_EXECUTABLE)
        # Security hardening check
        if(NOT ASCIICHAT_SKIP_HARDENING_VALIDATION)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DMODE=hardening
                    -DBINARY=$<TARGET_FILE:ascii-chat>
                    -DLLVM_READELF=${ASCIICHAT_LLVM_READELF_EXECUTABLE}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateBinary.cmake
                COMMENT "Validating security hardening"
                VERBATIM
            )
        endif()

        # Static linking check (USE_MUSL builds)
        if(USE_MUSL)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DMODE=static
                    -DBINARY=$<TARGET_FILE:ascii-chat>
                    -DLLVM_READELF=${ASCIICHAT_LLVM_READELF_EXECUTABLE}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateBinary.cmake
                COMMENT "Validating static linking"
                VERBATIM
            )
        endif()

        # Architecture check
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            set(EXPECTED_ARCH "x86_64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(EXPECTED_ARCH "aarch64")
        else()
            set(EXPECTED_ARCH "")
        endif()

        if(EXPECTED_ARCH)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -DMODE=architecture
                    -DBINARY=$<TARGET_FILE:ascii-chat>
                    -DLLVM_READELF=${ASCIICHAT_LLVM_READELF_EXECUTABLE}
                    -DEXPECTED_ARCH=${EXPECTED_ARCH}
                    -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateBinary.cmake
                COMMENT "Validating architecture (${EXPECTED_ARCH})"
                VERBATIM
            )
        endif()

        message(STATUS "Release binary validation: ${BoldGreen}enabled${ColorReset} (hardening, no_debug, static, architecture)")
    else()
        message(STATUS "Release binary validation: ${BoldYellow}disabled${ColorReset} (llvm-readelf not found)")
    endif()
endif()
