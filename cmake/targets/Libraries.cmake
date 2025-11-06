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

# Check if we're building OBJECT libraries (Windows dev builds)
if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage"))
    set(BUILDING_OBJECT_LIBS TRUE)
else()
    set(BUILDING_OBJECT_LIBS FALSE)
endif()

# Helper macro to create a module with common settings
macro(create_ascii_chat_module MODULE_NAME MODULE_SRCS)
    # For Windows Debug/Dev/Coverage builds: use OBJECT libraries so we can build a proper DLL
    # For other platforms/builds: use STATIC libraries
    if(BUILDING_OBJECT_LIBS)
        add_library(${MODULE_NAME} OBJECT ${MODULE_SRCS})
        # Mark all symbols for export when building DLL from OBJECT libraries
        target_compile_definitions(${MODULE_NAME} PRIVATE
            _WIN32_WINNT=0x0A00  # Windows 10
            BUILDING_ASCIICHAT_DLL=1
        )
    else()
        add_library(${MODULE_NAME} STATIC ${MODULE_SRCS})
        # For static library builds on Windows, define BUILDING_STATIC_LIB
        # so that ASCIICHAT_API expands to nothing (not dllimport)
        if(WIN32)
            target_compile_definitions(${MODULE_NAME} PRIVATE
                _WIN32_WINNT=0x0A00  # Windows 10
                BUILDING_STATIC_LIB=1
            )
        endif()
        # Enable Position Independent Code for shared library builds
        # Required for thread-local storage (TLS) relocations in shared objects
        set_target_properties(${MODULE_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    # Version dependency and global build timer
    add_dependencies(${MODULE_NAME} generate_version build-timer-start)

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
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-data-structures ascii-chat-util)
endif()

# -----------------------------------------------------------------------------
# Module 3: Platform Abstraction (depends on: util, data-structures)
# -----------------------------------------------------------------------------

create_ascii_chat_module(ascii-chat-platform "${PLATFORM_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-platform
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-core
    )
endif()

# Add kernel headers for musl builds (needed for V4L2)
if(USE_MUSL AND DEFINED MUSL_DEPS_DIR AND EXISTS "${MUSL_DEPS_DIR}/musl-deps/kernel-headers")
    target_include_directories(ascii-chat-platform PRIVATE
        "${MUSL_DEPS_DIR}/musl-deps/kernel-headers"
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
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-crypto
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-network
        ${LIBSODIUM_LIBRARIES}
    )
else()
    # For OBJECT libs, link external deps only (no module deps)
    target_link_libraries(ascii-chat-crypto ${LIBSODIUM_LIBRARIES})
endif()

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
        PROPERTIES COMPILE_OPTIONS "-Wno-sizeof-array-div;-Wno-unterminated-string-initialization"
    )
endif()

# -----------------------------------------------------------------------------
# Module 4: SIMD (depends on: util, core, video)
# Note: Circular dependency with video (simd needs video for benchmark code,
#       video needs simd for SIMD functions). This is resolved at link time
#       since both libraries are linked together in executables.
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-simd "${SIMD_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-simd
        ascii-chat-util
        ascii-chat-core
        ascii-chat-video
    )
endif()

# -----------------------------------------------------------------------------
# Module 5: Video Processing (depends on: util, platform, core, simd)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-video "${VIDEO_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-video
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-core
        ascii-chat-simd
    )
endif()

# -----------------------------------------------------------------------------
# Module 6: Audio Processing (depends on: util, platform, data-structures)
# -----------------------------------------------------------------------------
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
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-core
        ascii-chat-util
        ascii-chat-platform
    )
endif()

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
    target_link_libraries(ascii-chat-core ${MIMALLOC_LIB})
endif()

# -----------------------------------------------------------------------------
# Module 8: Network (depends on: util, platform, crypto, core)
# -----------------------------------------------------------------------------
create_ascii_chat_module(ascii-chat-network "${NETWORK_SRCS}")
if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-network
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-crypto
        ascii-chat-core
        ${ZSTD_LIBRARIES}
    )
else()
    # For OBJECT libs, link external deps only
    target_link_libraries(ascii-chat-network ${ZSTD_LIBRARIES})
endif()

# Core module was moved earlier in the dependency chain (Module 7)

# =============================================================================
# Unified Library Targets (OPTIONAL - not built by default)
# =============================================================================
# These combine all modules into a single library for external projects.
# Build with: cmake --build build --target ascii-chat-static
#         or: cmake --build build --target ascii-chat-shared
# =============================================================================

# Shared unified library (libasciichat.so / libasciichat.dylib / asciichat.dll)
# For Windows Debug/Dev/Coverage: Build from OBJECT libraries to enable proper symbol export
# For other platforms/builds: Link static libraries together
if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage"))
    # Windows dev builds: Create DLL from OBJECT libraries (modules compiled as OBJECT)
    add_library(ascii-chat-shared SHARED EXCLUDE_FROM_ALL
        $<TARGET_OBJECTS:ascii-chat-util>
        $<TARGET_OBJECTS:ascii-chat-data-structures>
        $<TARGET_OBJECTS:ascii-chat-platform>
        $<TARGET_OBJECTS:ascii-chat-crypto>
        $<TARGET_OBJECTS:ascii-chat-simd>
        $<TARGET_OBJECTS:ascii-chat-video>
        $<TARGET_OBJECTS:ascii-chat-audio>
        $<TARGET_OBJECTS:ascii-chat-network>
        $<TARGET_OBJECTS:ascii-chat-core>
    )
    set_target_properties(ascii-chat-shared PROPERTIES
        OUTPUT_NAME "asciichat"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}  # DLL goes in bin/
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # .so goes in lib/
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # Import .lib goes in lib/
        WINDOWS_EXPORT_ALL_SYMBOLS FALSE  # Use generated .def file instead
    )

    # Explicitly set import library location for Windows
    target_link_options(ascii-chat-shared PRIVATE "LINKER:/implib:${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")

    # Auto-generate .def file from OBJECT libraries
    # Use a wrapper script to collect object files and generate .def
    # Object files are in CMakeFiles/<target>.dir/ directories
    set(MODULE_TARGETS
        ascii-chat-util
        ascii-chat-data-structures
        ascii-chat-platform
        ascii-chat-crypto
        ascii-chat-simd
        ascii-chat-video
        ascii-chat-audio
        ascii-chat-network
        ascii-chat-core
    )

    # List of library module targets (exclude executable)
    set(MODULE_TARGETS
        ascii-chat-util;ascii-chat-data-structures;ascii-chat-platform
;ascii-chat-crypto;ascii-chat-simd;ascii-chat-video
;ascii-chat-audio;ascii-chat-network;ascii-chat-core
    )

    # Generate the .def file using a custom command
    # Convert list to string for passing to script
    string(REPLACE ";" "," MODULE_TARGETS_STR "${MODULE_TARGETS}")

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def
        COMMAND ${CMAKE_COMMAND}
            -DNM_TOOL=${CMAKE_NM}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DMODULE_TARGETS=${MODULE_TARGETS_STR}
            -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def
            -DLIBRARY_NAME=asciichat
            -P ${CMAKE_SOURCE_DIR}/cmake/targets/GenerateDefFile.cmake
        DEPENDS ${MODULE_TARGETS}
        COMMENT "Generating Windows .def file from object files"
        VERBATIM
    )

    # Create a target for the .def file generation
    add_custom_target(generate-def-file DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def)

    # Make the DLL depend on the .def file generation
    add_dependencies(ascii-chat-shared generate-def-file)

    # Use the generated .def file for linking
    target_link_options(ascii-chat-shared PRIVATE
        "LINKER:/DEF:${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def"
    )

    # Add include directories needed by the OBJECT libraries
    target_include_directories(ascii-chat-shared PRIVATE
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/include
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src
    )

    if(LIBSODIUM_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    endif()

    if(BEARSSL_FOUND)
        target_include_directories(ascii-chat-shared PRIVATE ${BEARSSL_INCLUDE_DIRS})
    endif()

    if(USE_MIMALLOC)
        target_include_directories(ascii-chat-shared PRIVATE ${FETCHCONTENT_BASE_DIR}/mimalloc-src/include)
    endif()

    # Add dependencies from modules
    add_dependencies(ascii-chat-shared generate_version)
    if(DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(ascii-chat-shared ${LIBSODIUM_BUILD_TARGET})
    endif()

    # Note: System library dependencies will be added below
else()
    # Unix or Windows Release: Compile shared library from all sources with default visibility
    # This allows the shared library to export symbols even though the project uses -fvisibility=hidden

    # Collect all source files from all modules
    set(ALL_LIBRARY_SRCS
        ${UTIL_SRCS}
        ${DATA_STRUCTURES_SRCS}
        ${PLATFORM_SRCS}
        ${CRYPTO_SRCS}
        ${SIMD_SRCS}
        ${VIDEO_SRCS}
        ${AUDIO_SRCS}
        ${NETWORK_SRCS}
        ${CORE_SRCS}
    )

    # Create shared library directly from sources (not from static libraries)
    add_library(ascii-chat-shared SHARED EXCLUDE_FROM_ALL ${ALL_LIBRARY_SRCS})
    set_target_properties(ascii-chat-shared PROPERTIES
        OUTPUT_NAME "asciichat"
        POSITION_INDEPENDENT_CODE ON
    )

    # Import library location for Windows Release builds
    # Release: .lib goes in lib/ (standard convention)
    # This applies to the else block which handles Release/non-Debug builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND WIN32)
        set_target_properties(ascii-chat-shared PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # .lib → lib/
        )
        target_link_options(ascii-chat-shared PRIVATE "LINKER:/implib:${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")
        message(STATUS "Release build: Import library will go to lib/")
    endif()

    # CRITICAL: Override flags for shared library compatibility (Unix only)
    # 1. -fvisibility=default: Export symbols for external use
    # 2. -ftls-model=global-dynamic: Correct TLS model for shared libraries
    # 3. -fPIC: Position-independent code required for shared libraries
    # 4. -fno-pie: Disable PIE mode (conflicts with -shared)
    # Note: Windows doesn't support these flags and uses different DLL export mechanisms
    if(NOT WIN32)
        target_compile_options(ascii-chat-shared PRIVATE
            -fvisibility=default
            -ftls-model=global-dynamic
            -fno-pie
            -fPIC
        )
    endif()

    # Add version dependency
    add_dependencies(ascii-chat-shared generate_version)

    # Include paths (same as modules) - PRIVATE because these are build-time only
    target_include_directories(ascii-chat-shared PRIVATE ${CMAKE_BINARY_DIR}/generated)

    # Windows DLL export flag (must be set when building the DLL)
    if(WIN32)
        target_compile_definitions(ascii-chat-shared PRIVATE
            _WIN32_WINNT=0x0A00  # Windows 10
            BUILDING_ASCIICHAT_DLL=1
        )
    endif()

    # Build directory for llvm-symbolizer --debug-file-directory (debug builds only)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        target_compile_definitions(ascii-chat-shared PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    endif()

    # MI_DEBUG for mimalloc
    if(DEFINED MIMALLOC_DEBUG_LEVEL)
        target_compile_definitions(ascii-chat-shared PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
    endif()

    # Mimalloc include directory
    if(USE_MIMALLOC)
        target_include_directories(ascii-chat-shared PRIVATE ${FETCHCONTENT_BASE_DIR}/mimalloc-src/include)
    endif()

    # Musl flag
    if(USE_MUSL)
        target_compile_definitions(ascii-chat-shared PRIVATE USE_MUSL=1)
    endif()

    # Crypto module dependencies (libsodium-bcrypt-pbkdf, libsodium, BearSSL)
    target_include_directories(ascii-chat-shared PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/include
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src
    )
    if(LIBSODIUM_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    endif()
    if(BEARSSL_FOUND)
        target_include_directories(ascii-chat-shared PRIVATE ${BEARSSL_INCLUDE_DIRS})
    endif()

    # Add dependency on libsodium build target if building from source
    if(DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(ascii-chat-shared ${LIBSODIUM_BUILD_TARGET})
    endif()

    # Disable specific warnings for bcrypt_pbkdf.c (third-party code with false positives)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(
            ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src/openbsd-compat/bcrypt_pbkdf.c
            PROPERTIES COMPILE_OPTIONS "-Wno-sizeof-array-div;-Wno-unterminated-string-initialization"
        )
    endif()

    # Platform-specific linker flags
    if(APPLE)
        # Export all symbols on macOS
        target_link_options(ascii-chat-shared PRIVATE -Wl,-export_dynamic)
    elseif(WIN32)
        # Windows Release: DLL export configuration
        # Use WINDOWS_EXPORT_ALL_SYMBOLS to automatically export all symbols
        set_target_properties(ascii-chat-shared PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS TRUE
        )
    else()
        # Linux: Export all symbols using version script
        # Remove executable-specific optimizations that break shared library symbol export:
        # - No --gc-sections (removes "unused" symbols meant for external users)
        # - No -pie (for executables, not shared libraries)
        # - No hardening flags (breaks dynamic symbol export)
        target_link_options(ascii-chat-shared PRIVATE
            -Wl,--version-script=${CMAKE_SOURCE_DIR}/cmake/install/export_all.lds
        )
        # Clear LINK_OPTIONS property to remove global flags (pie, gc-sections, hardening)
        # then add only what we need
        set_target_properties(ascii-chat-shared PROPERTIES
            LINK_OPTIONS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/cmake/install/export_all.lds"
        )
        # Note: The "_start not found" linker warning is harmless for shared libraries
        # (they don't need an entry point like executables do)
    endif()
endif()

# Add system library dependencies
if(WIN32)
    target_link_libraries(ascii-chat-shared PRIVATE
        ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
        ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
        crypt32  # For Windows crypto certificate functions (matches ascii-chat-platform)
        Winmm    # For timeBeginPeriod/timeEndPeriod
        ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    # Windows DLL needs mimalloc built in (no LD_PRELOAD mechanism)
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
else()
    # For musl builds, shared library links against system glibc libraries
    # Musl-built static libraries use incompatible TLS model (local-dynamic) and cannot be embedded in .so
    # Shared library users will have glibc, so we use system packages via pkg-config
    if(USE_MUSL)
        # Use system glibc libraries for shared library (not musl static libs)
        # Users of libasciichat.so will have glibc available
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PORTAUDIO_SYS portaudio-2.0)
        pkg_check_modules(ZSTD_SYS libzstd)
        pkg_check_modules(LIBSODIUM_SYS libsodium)

        target_link_libraries(ascii-chat-shared PRIVATE
            ${PORTAUDIO_SYS_LIBRARIES}
            ${ZSTD_SYS_LIBRARIES}
            ${LIBSODIUM_SYS_LIBRARIES}
            m
            ${CMAKE_THREAD_LIBS_INIT}
        )
        target_include_directories(ascii-chat-shared PRIVATE
            ${PORTAUDIO_SYS_INCLUDE_DIRS}
            ${ZSTD_SYS_INCLUDE_DIRS}
            ${LIBSODIUM_SYS_INCLUDE_DIRS}
        )

        # Link BearSSL and mimalloc static libraries into shared library
        # These are compiled with -fPIC and correct TLS model for shared library compatibility
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
        endif()
        if(USE_MIMALLOC)
            # Use mimalloc-shared target (compiled with -fPIC and global-dynamic TLS)
            target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_SHARED_LIBRARIES})
        endif()
    else()
        # Non-musl builds use normal dependencies
        target_link_libraries(ascii-chat-shared PRIVATE
            ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES} m
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
        endif()
        # Link mimalloc into shared library (required for SAFE_MALLOC macros)
        if(USE_MIMALLOC AND TARGET mimalloc-shared)
            target_link_libraries(ascii-chat-shared PRIVATE mimalloc-shared)
        endif()
        if(PLATFORM_DARWIN)
            target_link_libraries(ascii-chat-shared PRIVATE
                ${FOUNDATION_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
                ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
            )
        elseif(PLATFORM_LINUX)
            target_link_libraries(ascii-chat-shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
        endif()
    endif()
endif()

# Add build timing for ascii-chat-shared library
# Record start time before linking (only when actually building)
add_custom_command(TARGET ascii-chat-shared PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    COMMENT "Recording build start time for ascii-chat-shared"
    COMMAND_ECHO NONE
    VERBATIM
)

# Show timing after build completes (only when actually building)
add_custom_command(TARGET ascii-chat-shared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    COMMENT ""
    COMMAND_ECHO NONE
    VERBATIM
)

# =============================================================================
# Combined Static Library (only when building STATIC libs, not OBJECT libs)
# =============================================================================
# Create libasciichat.a by combining all module static libraries
# Only available when modules are STATIC libraries (not OBJECT libraries)
# On Windows Debug/Dev/Coverage, modules are OBJECT libraries for DLL building
if(NOT BUILDING_OBJECT_LIBS)
if(APPLE)
    # macOS: Use libtool to combine static libraries
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
        COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/lib
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
        COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
        DEPENDS
            ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining module libraries into libasciichat.a"
        COMMAND_ECHO NONE
        COMMAND_EXPAND_LISTS
    )
else()
    # Linux/Windows: Use ar MRI script to combine archives
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
        COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/lib
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
        COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
        DEPENDS
            ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining module libraries into libasciichat.a"
        COMMAND_EXPAND_LISTS
    )
endif()

# Create interface library target that wraps the combined static library
# and propagates all external dependencies
add_library(ascii-chat-static-lib INTERFACE)
target_link_libraries(ascii-chat-static-lib INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
)

# Propagate external dependencies from individual libraries
# These are needed because the combined library only contains object files
# External dependencies must be linked separately

# Cryptography dependencies (from ascii-chat-crypto)
target_link_libraries(ascii-chat-static-lib INTERFACE ${LIBSODIUM_LIBRARIES})
if(BEARSSL_FOUND)
    target_link_libraries(ascii-chat-static-lib INTERFACE ${BEARSSL_LIBRARIES})
endif()

# Network dependencies (from ascii-chat-network)
target_link_libraries(ascii-chat-static-lib INTERFACE ${ZSTD_LIBRARIES})

# Audio dependencies (from ascii-chat-audio)
target_link_libraries(ascii-chat-static-lib INTERFACE ${PORTAUDIO_LIBRARIES})
if(UNIX AND NOT APPLE AND NOT USE_MUSL)
    target_link_libraries(ascii-chat-static-lib INTERFACE jack)
endif()

# Memory allocator (mimalloc)
if(MIMALLOC_LIBRARIES)
    target_link_libraries(ascii-chat-static-lib INTERFACE ${MIMALLOC_LIBRARIES})
endif()

# Platform-specific system libraries (from ascii-chat-platform)
if(WIN32)
    target_link_libraries(ascii-chat-static-lib INTERFACE
        ${WS2_32_LIB}
        ${USER32_LIB}
        ${ADVAPI32_LIB}
        ${DBGHELP_LIB}
        ${MF_LIB}
        ${MFPLAT_LIB}
        ${MFREADWRITE_LIB}
        ${MFUUID_LIB}
        ${OLE32_LIB}
        crypt32
    )
else()
    if(PLATFORM_DARWIN)
        target_link_libraries(ascii-chat-static-lib INTERFACE
            ${FOUNDATION_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
        )
    elseif(PLATFORM_LINUX)
        target_link_libraries(ascii-chat-static-lib INTERFACE ${CMAKE_THREAD_LIBS_INIT})
    endif()
    # Math library (from ascii-chat-core)
    target_link_libraries(ascii-chat-static-lib INTERFACE m)
endif()

# User-friendly 'static-lib' target - builds the static library
add_custom_target(static-lib
    COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a build-timer-start
    VERBATIM
)

# Build target for Release builds (referenced by Executables.cmake)
add_custom_target(ascii-chat-static-build
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
    VERBATIM
)
endif() # NOT BUILDING_OBJECT_LIBS

# =============================================================================
# Unified Library (ascii-chat-static for backward compatibility)
# =============================================================================
# Debug/Dev/Coverage: Shared library (DLL on Windows) for faster linking during development
# Release: Static library for distribution
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage")
    # Use the existing ascii-chat-shared target but make it build by default
    # Remove EXCLUDE_FROM_ALL for Debug/Dev/Coverage builds
    set_target_properties(ascii-chat-shared PROPERTIES EXCLUDE_FROM_ALL FALSE)

    # Create interface library that wraps the shared library
    # Link directly to the target (CMake handles the import library on Windows)
    add_library(ascii-chat-static INTERFACE)
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-shared)

    # Build target name for consistency
    set(ASCII_CHAT_UNIFIED_BUILD_TARGET ascii-chat-shared)
else()
    # Release builds: ascii-chat-static wraps the combined static library
    add_library(ascii-chat-static INTERFACE)
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-static-lib)

    # Build target name for consistency
    set(ASCII_CHAT_UNIFIED_BUILD_TARGET ascii-chat-static-build)
endif()

# Note: External dependencies (mimalloc, libsodium, portaudio, etc.) are linked
# to the individual module libraries. When the executable links against
# ascii-chat-static, it will get all the object files from the combined library,
# but external dependencies still need to be linked to the executable
# (this is handled in Executables.cmake).

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
message(STATUS "Unified libraries:")
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev" OR CMAKE_BUILD_TYPE STREQUAL "Coverage")
    if(WIN32)
        message(STATUS "  Default (${CMAKE_BUILD_TYPE}): ${BoldCyan}ascii-chat-static${ColorReset} → ${BoldBlue}asciichat.dll${ColorReset} (shared)")
    else()
        message(STATUS "  Default (${CMAKE_BUILD_TYPE}): ${BoldCyan}ascii-chat-static${ColorReset} → ${BoldBlue}libasciichat${CMAKE_SHARED_LIBRARY_SUFFIX}${ColorReset} (shared)")
    endif()
else()
    message(STATUS "  Default (${CMAKE_BUILD_TYPE}): ${BoldCyan}ascii-chat-static${ColorReset} → ${BoldBlue}libasciichat.a${ColorReset} (static)")
endif()
message(STATUS "  Static library:       ${BoldGreen}static-lib${ColorReset} → ${BoldBlue}libasciichat.a${ColorReset} (always static)")
message(STATUS "  Optional shared:      ${BoldCyan}ascii-chat-shared${ColorReset} → ${BoldBlue}libasciichat.so/.dylib/.dll${ColorReset} (EXCLUDE_FROM_ALL)")
message(STATUS "")

