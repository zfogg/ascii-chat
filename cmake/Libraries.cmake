# =============================================================================
# Libraries Module
# =============================================================================
# This module creates all library targets and their dependencies
#
# Prerequisites:
#   - All *_SRCS variables must be set (via SourceFiles.cmake)
#   - Platform detection complete
#   - Dependencies found (via Dependencies.cmake)
#   - USE_MIMALLOC, USE_MUSL known
#
# Outputs:
#   - All ascii-chat-* library targets
#   - ascii-chat-lib interface library
#   - ascii-chat-static and ascii-chat-shared unified libraries
# =============================================================================

# Helper macro to create a module with common settings
macro(create_ascii_chat_module MODULE_NAME MODULE_SRCS)
    add_library(${MODULE_NAME} STATIC ${MODULE_SRCS})

    # Version dependency
    add_dependencies(${MODULE_NAME} generate_version)

    # Include paths
    target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_BINARY_DIR}/generated)

    # Build directory for llvm-symbolizer --debug-file-directory (debug builds only)
    # Only include BUILD_DIR in debug builds to avoid embedding build paths in release binaries
    # Note: Release builds will not have BUILD_DIR defined, so llvm-symbolizer will work without it
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        target_compile_definitions(${MODULE_NAME} PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    endif()

    # MI_DEBUG for mimalloc
    if(DEFINED MIMALLOC_DEBUG_LEVEL)
        target_compile_definitions(${MODULE_NAME} PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
    endif()

    # Mimalloc include directory
    if(USE_MIMALLOC)
        target_include_directories(${MODULE_NAME} PRIVATE ${FETCHCONTENT_BASE_DIR}/mimalloc-src/include)
    endif()

    # Musl flag
    if(USE_MUSL)
        target_compile_definitions(${MODULE_NAME} PRIVATE USE_MUSL=1)
    endif()
endmacro()

# -----------------------------------------------------------------------------
# Module 1: Utilities (no dependencies)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-util "${UTIL_SRCS}")

# -----------------------------------------------------------------------------
# Module 2: Data Structures (depends on: util)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-data-structures "${DATA_STRUCTURES_SRCS}")
target_link_libraries(ascii-chat-data-structures ascii-chat-util)

# -----------------------------------------------------------------------------
# Module 3: Platform Abstraction (depends on: util, data-structures)
# -----------------------------------------------------------------------------

create_ascii_chat_module(ascii-chat-platform "${PLATFORM_SRCS}")
target_link_libraries(ascii-chat-platform
    ascii-chat-util
    ascii-chat-data-structures
    ascii-chat-core
)

# Add kernel headers for musl builds (needed for V4L2)
if(USE_MUSL AND EXISTS "${FETCHCONTENT_BASE_DIR}/musl-deps/kernel-headers")
    target_include_directories(ascii-chat-platform PRIVATE
        "${FETCHCONTENT_BASE_DIR}/musl-deps/kernel-headers"
    )
endif()

# Platform-specific system libraries
if(WIN32)
    target_link_libraries(ascii-chat-platform
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${DBGHELP_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        crypt32  # For Windows crypto certificate functions
    )
else()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-platform
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-platform ${CMAKE_THREAD_LIBS_INIT})
    endif()
endif()

# -----------------------------------------------------------------------------
# Module 3: Cryptography (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-crypto "${CRYPTO_SRCS}")
target_link_libraries(ascii-chat-crypto
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-network
    ${LIBSODIUM_LIBRARIES}
)

# Add libsodium include directory (for GCC builds from source)
if(LIBSODIUM_INCLUDE_DIRS)
    target_include_directories(ascii-chat-crypto PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
endif()

# Add dependency on libsodium build target if building from source
if(DEFINED LIBSODIUM_BUILD_TARGET)
    add_dependencies(ascii-chat-crypto ${LIBSODIUM_BUILD_TARGET})
endif()


# Add BearSSL if available
if(BEARSSL_FOUND)
    target_link_libraries(ascii-chat-crypto ${BEARSSL_LIBRARIES})
    target_include_directories(ascii-chat-crypto PRIVATE ${BEARSSL_INCLUDE_DIRS})
endif()

# Add libsodium-bcrypt-pbkdf include directory
target_include_directories(ascii-chat-crypto PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/include
    ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src
)

# Disable specific warnings for bcrypt_pbkdf.c (third-party code with false positives)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
        PROPERTIES COMPILE_OPTIONS "-Wno-unterminated-string-initialization;-Wno-sizeof-array-div"
    )
endif()

# -----------------------------------------------------------------------------
# Module 4: SIMD (depends on: util, core, video)
# Note: Circular dependency with video (simd needs video for benchmark code,
#       video needs simd for SIMD functions). This is resolved at link time
#       since both libraries are linked together in executables.
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-simd "${SIMD_SRCS}")
target_link_libraries(ascii-chat-simd
    ascii-chat-util
    ascii-chat-core
    ascii-chat-video
)

# -----------------------------------------------------------------------------
# Module 5: Video Processing (depends on: util, platform, core, simd)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-video "${VIDEO_SRCS}")
target_link_libraries(ascii-chat-video
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-core
    ascii-chat-simd
)

# -----------------------------------------------------------------------------
# Module 6: Audio Processing (depends on: util, platform, data-structures)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-audio "${AUDIO_SRCS}")
target_link_libraries(ascii-chat-audio
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-data-structures
    ${PORTAUDIO_LIBRARIES}
)

# Link JACK on Linux (system PortAudio is built with JACK support)
# macOS uses CoreAudio, Windows uses WASAPI/DirectSound
# Note: musl builds use PortAudio with ALSA only (no JACK) to avoid static lib dependency
if(UNIX AND NOT APPLE AND NOT USE_MUSL)
    target_link_libraries(ascii-chat-audio jack)
endif()

# -----------------------------------------------------------------------------
# Module 7: Core Infrastructure (depends on: util, platform)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-core "${CORE_SRCS}")
target_link_libraries(ascii-chat-core
    ascii-chat-util
    ascii-chat-platform
)

# Math library (Unix only - Windows has math functions in C runtime)
if(NOT WIN32)
    target_link_libraries(ascii-chat-core m)
endif()

# Special musl handling for libexecinfo
if(USE_MUSL)
    target_link_libraries(ascii-chat-core ${MUSL_PREFIX}/lib/libexecinfo.a)
    add_dependencies(ascii-chat-core libexecinfo-musl)
endif()

# mimalloc for core
if(USE_MIMALLOC)
    target_link_libraries(ascii-chat-core ${MIMALLOC_LIB})
endif()

# -----------------------------------------------------------------------------
# Module 8: Network (depends on: util, platform, crypto, core)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-network "${NETWORK_SRCS}")
target_link_libraries(ascii-chat-network
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-crypto
    ascii-chat-core
    ${ZSTD_LIBRARIES}
)

# Core module was moved earlier in the dependency chain (Module 7)

# =============================================================================
# Unified Library Targets (OPTIONAL - not built by default)
# =============================================================================
# These combine all modules into a single library for external projects.
# Build with: cmake --build build --target ascii-chat-static
#         or: cmake --build build --target ascii-chat-shared
# =============================================================================

# Shared unified library (libasciichat.so / libasciichat.dylib / asciichat.dll)
# Links all module static libraries together without recompiling source code
# macOS note: We use -Wl,-all_load to force inclusion of all symbols from static libs
# Linux note: We use -Wl,--whole-archive to force inclusion of all symbols
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/empty.c "// Empty file for shared library\n")
add_library(ascii-chat-shared SHARED EXCLUDE_FROM_ALL ${CMAKE_CURRENT_BINARY_DIR}/empty.c)
set_target_properties(ascii-chat-shared PROPERTIES OUTPUT_NAME "asciichat")

# Link all module libraries
if(APPLE)
    target_link_libraries(ascii-chat-shared PRIVATE
        -Wl,-all_load
        ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
        ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
    )
else()
    target_link_libraries(ascii-chat-shared PRIVATE
        -Wl,--whole-archive
        ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
        ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        -Wl,--no-whole-archive
    )
endif()

# Add system library dependencies
if(WIN32)
    target_link_libraries(ascii-chat-shared PRIVATE
        ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
        ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
        ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
else()
    target_link_libraries(ascii-chat-shared PRIVATE
        ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES} m
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-shared PRIVATE
            ${FOUNDATION_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
    endif()
    if(USE_MUSL)
        target_link_libraries(ascii-chat-shared PRIVATE ${MUSL_PREFIX}/lib/libexecinfo.a)
        add_dependencies(ascii-chat-shared libexecinfo-musl)
    endif()
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
endif()

# Static unified library (libasciichat.a)
# Extracts object files from module .a files and combines them without recompiling
# macOS uses libtool -static, Linux uses ar MRI script
if(APPLE)
    # macOS: Use libtool to combine static libraries
    add_custom_target(ascii-chat-static
        COMMAND libtool -static -o ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
            $<TARGET_FILE:ascii-chat-util>
            $<TARGET_FILE:ascii-chat-data-structures>
            $<TARGET_FILE:ascii-chat-platform>
            $<TARGET_FILE:ascii-chat-crypto>
            $<TARGET_FILE:ascii-chat-simd>
            $<TARGET_FILE:ascii-chat-video>
            $<TARGET_FILE:ascii-chat-audio>
            $<TARGET_FILE:ascii-chat-network>
            $<TARGET_FILE:ascii-chat-core>
        DEPENDS
            ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining module libraries into libasciichat.a (no recompilation)"
        COMMAND_EXPAND_LISTS
    )
else()
    # Linux/Windows: Use ar MRI script to combine archives
    add_custom_target(ascii-chat-static
        COMMAND ${CMAKE_COMMAND} -E echo "CREATE ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a" > ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-util>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-data-structures>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-platform>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-crypto>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-simd>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-video>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-audio>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-network>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "ADDLIB $<TARGET_FILE:ascii-chat-core>" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "SAVE" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_COMMAND} -E echo "END" >> ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        COMMAND ${CMAKE_AR} -M < ${CMAKE_CURRENT_BINARY_DIR}/combine.mri
        DEPENDS
            ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining module libraries into libasciichat.a (no recompilation)"
        COMMAND_EXPAND_LISTS
    )
endif()

# =============================================================================
# Library Alias for Tests
# =============================================================================
# Create an alias that links all modules for test compatibility
# This allows tests to link against ascii-chat-lib instead of individual modules
add_library(ascii-chat-lib INTERFACE)
target_link_libraries(ascii-chat-lib INTERFACE
    ascii-chat-util
    ascii-chat-platform
    ascii-chat-crypto
    ascii-chat-simd
    ascii-chat-video
    ascii-chat-audio
    ascii-chat-network
    ascii-chat-core
)

message(STATUS "")
message(STATUS "=== Modular Library Architecture ===")
message(STATUS "Individual modules: ascii-chat-util, ascii-chat-platform, ascii-chat-crypto,")
message(STATUS "                    ascii-chat-simd, ascii-chat-video, ascii-chat-audio,")
message(STATUS "                    ascii-chat-network, ascii-chat-core")
message(STATUS "")
message(STATUS "Optional unified libraries (not built by default):")
message(STATUS "  Static:  cmake --build build --target ascii-chat-static  → libasciichat.a")
message(STATUS "  Shared:  cmake --build build --target ascii-chat-shared  → libasciichat.so/.dylib/.dll")
message(STATUS "")

