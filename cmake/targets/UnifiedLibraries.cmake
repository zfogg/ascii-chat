# =============================================================================
# Unified Library Targets
# =============================================================================
# Creates combined static and shared library targets from individual modules.
#
# Prerequisites:
#   - All ascii-chat-* module targets must be created (via lib/CMakeLists.txt)
#   - Platform detection complete
#   - Dependencies found (via Dependencies.cmake)
#   - USE_MIMALLOC, USE_MUSL known
#   - BUILDING_OBJECT_LIBS set (via ModuleHelpers.cmake)
#
# Outputs:
#   - ascii-chat-shared: Shared library (libasciichat.so / libasciichat.dylib / asciichat.dll)
#   - ascii-chat-static: Interface library wrapping combined static library
#   - ascii-chat-static-lib: Interface for the combined .a file
#   - static-lib: User-friendly target to build static library
# =============================================================================

# =============================================================================
# Shared Library (ascii-chat-shared)
# =============================================================================
# For Windows Debug/Dev: Build from OBJECT libraries to enable proper symbol export
# For other platforms/builds: Compile from all sources with default visibility

if(WIN32 AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev"))
    # Windows dev builds: Create DLL from OBJECT libraries (modules compiled as OBJECT)
    # Note: ascii-chat-panic is always included as it provides runtime logging functions
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
        $<TARGET_OBJECTS:ascii-chat-panic>
    )
    if(ASCIICHAT_ENABLE_UNITY_BUILDS)
        set_target_properties(ascii-chat-shared PROPERTIES UNITY_BUILD ON)
    endif()
    if(ASCIICHAT_ENABLE_IPO)
        set_property(TARGET ascii-chat-shared PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    set_target_properties(ascii-chat-shared PROPERTIES
        OUTPUT_NAME "asciichat"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}  # DLL goes in bin/
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # .so goes in lib/
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}  # Import .lib goes in lib/
        WINDOWS_EXPORT_ALL_SYMBOLS FALSE  # Use generated .def file instead
        VERSION ${ASCIICHAT_LIB_VERSION}
        SOVERSION ${ASCIICHAT_LIB_VERSION_MAJOR}
    )

    # Explicitly set import library location for Windows
    target_link_options(ascii-chat-shared PRIVATE "LINKER:/implib:${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")

    # Auto-generate .def file from OBJECT libraries
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
        ascii-chat-panic
    )

    # Generate the .def file using a custom command
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

    add_custom_target(generate-def-file DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def)
    add_dependencies(ascii-chat-shared generate-def-file)

    target_link_options(ascii-chat-shared PRIVATE
        "LINKER:/DEF:${CMAKE_CURRENT_BINARY_DIR}/asciichat_generated.def"
    )

    # Add include directories needed by the OBJECT libraries
    target_include_directories(ascii-chat-shared PRIVATE
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/include
        ${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf/src
    )

    if(LIBSODIUM_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    endif()

    if(BEARSSL_FOUND)
        target_include_directories(ascii-chat-shared PRIVATE ${BEARSSL_INCLUDE_DIRS})
    endif()

    if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE $<BUILD_INTERFACE:${MIMALLOC_INCLUDE_DIRS}>)
    endif()

    add_dependencies(ascii-chat-shared generate_version)
    if(DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(ascii-chat-shared ${LIBSODIUM_BUILD_TARGET})
    endif()

else()
    # Unix or Windows Release: Compile shared library from all sources with default visibility
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
        ${TOOLING_PANIC_SRCS}
    )

    add_library(ascii-chat-shared SHARED EXCLUDE_FROM_ALL ${ALL_LIBRARY_SRCS})
    set_target_properties(ascii-chat-shared PROPERTIES
        OUTPUT_NAME "asciichat"
        POSITION_INDEPENDENT_CODE ON
        VERSION ${ASCIICHAT_LIB_VERSION}
        SOVERSION ${ASCIICHAT_LIB_VERSION_MAJOR}
    )

    if(ASCIICHAT_ENABLE_UNITY_BUILDS)
        set_target_properties(ascii-chat-shared PROPERTIES UNITY_BUILD ON)
    endif()

    if(ASCIICHAT_ENABLE_IPO)
        set_property(TARGET ascii-chat-shared PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    # Release: .lib goes in lib/
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND WIN32)
        set_target_properties(ascii-chat-shared PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        )
        target_link_options(ascii-chat-shared PRIVATE "LINKER:/implib:${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")
        message(STATUS "Release build: Import library will go to lib/")
    endif()

    # Override flags for shared library compatibility (Unix only)
    if(NOT WIN32)
        target_compile_options(ascii-chat-shared PRIVATE
            -fvisibility=default
            -ftls-model=global-dynamic
            -fno-pie
            -fPIC
        )
    endif()

    add_dependencies(ascii-chat-shared generate_version)

    target_include_directories(ascii-chat-shared PRIVATE ${CMAKE_BINARY_DIR}/generated)

    if(WIN32)
        target_compile_definitions(ascii-chat-shared PRIVATE
            _WIN32_WINNT=0x0A00
            BUILDING_ASCIICHAT_DLL=1
        )
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "Dev")
        target_compile_definitions(ascii-chat-shared PRIVATE BUILD_DIR="${CMAKE_BINARY_DIR}")
    endif()

    if(DEFINED MIMALLOC_DEBUG_LEVEL)
        target_compile_definitions(ascii-chat-shared PRIVATE MI_DEBUG=${MIMALLOC_DEBUG_LEVEL})
    endif()

    if(USE_MIMALLOC AND MIMALLOC_INCLUDE_DIRS)
        target_include_directories(ascii-chat-shared PRIVATE $<BUILD_INTERFACE:${MIMALLOC_INCLUDE_DIRS}>)
    endif()

    if(USE_MUSL)
        target_compile_definitions(ascii-chat-shared PRIVATE USE_MUSL=1)
    endif()

    # Crypto module dependencies
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

    if(DEFINED LIBSODIUM_BUILD_TARGET)
        add_dependencies(ascii-chat-shared ${LIBSODIUM_BUILD_TARGET})
    endif()

    # Platform-specific linker flags
    if(APPLE)
        # macOS: Symbols are exported by default when using -fvisibility=default
    elseif(WIN32)
        set_target_properties(ascii-chat-shared PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS TRUE
        )
    else()
        # Linux: Export all symbols using version script
        target_link_options(ascii-chat-shared PRIVATE
            -Wl,--version-script=${CMAKE_SOURCE_DIR}/cmake/install/export_all.lds
        )
        set_target_properties(ascii-chat-shared PROPERTIES
            LINK_OPTIONS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/cmake/install/export_all.lds"
        )
    endif()
endif()

# =============================================================================
# Shared Library System Dependencies
# =============================================================================
if(WIN32)
    target_link_libraries(ascii-chat-shared PRIVATE
        ${WS2_32_LIB} ${USER32_LIB} ${ADVAPI32_LIB} ${DBGHELP_LIB}
        ${MF_LIB} ${MFPLAT_LIB} ${MFREADWRITE_LIB} ${MFUUID_LIB} ${OLE32_LIB}
        crypt32 Winmm
        ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES}
    )
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
    endif()
    if(USE_MIMALLOC)
        target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
    endif()
else()
    if(USE_MUSL)
        # Use system glibc libraries for shared library
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

        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
        endif()
        if(USE_MIMALLOC)
            if(TARGET mimalloc-shared)
                add_dependencies(ascii-chat-shared mimalloc-shared)
                target_link_libraries(ascii-chat-shared PRIVATE
                    -Wl,--whole-archive $<TARGET_FILE:mimalloc-shared> -Wl,--no-whole-archive)
            elseif(ASCIICHAT_MIMALLOC_SHARED_LINK_LIB)
                target_link_libraries(ascii-chat-shared PRIVATE
                    -Wl,--whole-archive ${ASCIICHAT_MIMALLOC_SHARED_LINK_LIB} -Wl,--no-whole-archive)
            elseif(MIMALLOC_SHARED_LIBRARIES)
                target_link_libraries(ascii-chat-shared PRIVATE
                    -Wl,--whole-archive ${MIMALLOC_SHARED_LIBRARIES} -Wl,--no-whole-archive)
            endif()
        endif()
    else()
        # Non-musl builds use normal dependencies
        target_link_libraries(ascii-chat-shared PRIVATE
            ${PORTAUDIO_LIBRARIES} ${ZSTD_LIBRARIES} ${LIBSODIUM_LIBRARIES} m
        )
        if(BEARSSL_FOUND)
            target_link_libraries(ascii-chat-shared PRIVATE ${BEARSSL_LIBRARIES})
        endif()
        if(USE_MIMALLOC)
            if(MIMALLOC_IS_SHARED_LIB)
                if(TARGET mimalloc-shared)
                    target_link_libraries(ascii-chat-shared PRIVATE mimalloc-shared)
                elseif(MIMALLOC_LIBRARIES)
                    target_link_libraries(ascii-chat-shared PRIVATE ${MIMALLOC_LIBRARIES})
                endif()
            elseif(TARGET mimalloc-shared)
                add_dependencies(ascii-chat-shared mimalloc-shared)
                if(APPLE)
                    target_link_libraries(ascii-chat-shared PRIVATE -force_load $<TARGET_FILE:mimalloc-shared>)
                else()
                    target_link_libraries(ascii-chat-shared PRIVATE
                        -Wl,--whole-archive $<TARGET_FILE:mimalloc-shared> -Wl,--no-whole-archive)
                endif()
            elseif(ASCIICHAT_MIMALLOC_SHARED_LINK_LIB)
                if(APPLE)
                    target_link_libraries(ascii-chat-shared PRIVATE -force_load ${ASCIICHAT_MIMALLOC_SHARED_LINK_LIB})
                else()
                    target_link_libraries(ascii-chat-shared PRIVATE
                        -Wl,--whole-archive ${ASCIICHAT_MIMALLOC_SHARED_LINK_LIB} -Wl,--no-whole-archive)
                endif()
            elseif(MIMALLOC_SHARED_LIBRARIES)
                if(APPLE)
                    target_link_libraries(ascii-chat-shared PRIVATE -force_load ${MIMALLOC_SHARED_LIBRARIES})
                else()
                    target_link_libraries(ascii-chat-shared PRIVATE
                        -Wl,--whole-archive ${MIMALLOC_SHARED_LIBRARIES} -Wl,--no-whole-archive)
                endif()
            endif()
        endif()
        if(PLATFORM_DARWIN)
            target_link_libraries(ascii-chat-shared PRIVATE
                ${FOUNDATION_FRAMEWORK} ${AVFOUNDATION_FRAMEWORK}
                ${COREMEDIA_FRAMEWORK} ${COREVIDEO_FRAMEWORK}
                ${COREAUDIO_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK}
                ${CORESERVICES_FRAMEWORK}
            )
        elseif(PLATFORM_LINUX)
            target_link_libraries(ascii-chat-shared PRIVATE ${CMAKE_THREAD_LIBS_INIT})
            if(JACK_LIBRARY AND NOT USE_MUSL)
                target_link_libraries(ascii-chat-shared PRIVATE ${JACK_LIBRARY})
            endif()
        endif()
    endif()
endif()

# Add build timing for ascii-chat-shared library
add_custom_command(TARGET ascii-chat-shared PRE_LINK
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Starting ascii-chat-shared timer"
    VERBATIM
)
add_custom_command(TARGET ascii-chat-shared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=ascii-chat-shared -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMENT "Finishing ascii-chat-shared timer"
    VERBATIM
)

# =============================================================================
# Symbol Validation
# =============================================================================
set(ASCIICHAT_REQUIRED_SYMBOLS
    "log_msg"
    "ascii_thread_create"
    "ascii_thread_join"
    "mutex_init"
    "mutex_lock"
    "mutex_unlock"
    "send_packet"
    "receive_packet"
    "crypto_init"
)

set(ASCIICHAT_INTERNAL_SYMBOLS "")

if(USE_MIMALLOC AND NOT MIMALLOC_IS_SHARED_LIB AND NOT WIN32)
    list(APPEND ASCIICHAT_INTERNAL_SYMBOLS "mi_malloc" "mi_free")
endif()

string(REPLACE ";" "," ASCIICHAT_SYMBOLS_CSV "${ASCIICHAT_REQUIRED_SYMBOLS}")
if(ASCIICHAT_INTERNAL_SYMBOLS)
    string(REPLACE ";" "," ASCIICHAT_INTERNAL_SYMBOLS_CSV "${ASCIICHAT_INTERNAL_SYMBOLS}")
endif()

# Validate symbols in shared library after build
if(ASCIICHAT_LLVM_NM_EXECUTABLE)
    if(WIN32)
        set(SHARED_LIB_TO_VALIDATE "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/asciichat.lib")
    else()
        set(SHARED_LIB_TO_VALIDATE "$<TARGET_FILE:ascii-chat-shared>")
    endif()

    set(_VALIDATE_CMD
        ${CMAKE_COMMAND}
        -DLLVM_NM=${ASCIICHAT_LLVM_NM_EXECUTABLE}
        -DLIBRARY=${SHARED_LIB_TO_VALIDATE}
        -DSYMBOLS=${ASCIICHAT_SYMBOLS_CSV}
    )
    if(ASCIICHAT_INTERNAL_SYMBOLS_CSV)
        list(APPEND _VALIDATE_CMD -DINTERNAL_SYMBOLS=${ASCIICHAT_INTERNAL_SYMBOLS_CSV})
    endif()
    list(APPEND _VALIDATE_CMD -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateSymbols.cmake)

    add_custom_command(TARGET ascii-chat-shared POST_BUILD
        COMMAND ${_VALIDATE_CMD}
        COMMENT "Validating ascii-chat symbols in shared library"
        VERBATIM
    )
endif()

# =============================================================================
# Combined Static Library (only when building STATIC libs, not OBJECT libs)
# =============================================================================
if(NOT BUILDING_OBJECT_LIBS)
    # Use ar MRI script to combine archives across platforms
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
        COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
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
        COMMAND ${CMAKE_COMMAND} -DACTION=end -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        DEPENDS
            ascii-chat-util ascii-chat-data-structures ascii-chat-platform ascii-chat-crypto ascii-chat-simd
            ascii-chat-video ascii-chat-audio ascii-chat-network ascii-chat-core
        COMMENT "Combining static libraries into libasciichat.a"
        COMMAND_EXPAND_LISTS
    )

    # Create interface library target that wraps the combined static library
    add_library(ascii-chat-static-lib INTERFACE)
    target_link_libraries(ascii-chat-static-lib INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a>
        $<INSTALL_INTERFACE:lib/libasciichat.a>
    )

    # Propagate external dependencies from individual libraries
    target_link_libraries(ascii-chat-static-lib INTERFACE ${LIBSODIUM_LIBRARIES})
    if(BEARSSL_FOUND)
        target_link_libraries(ascii-chat-static-lib INTERFACE ${BEARSSL_LIBRARIES})
    endif()

    target_link_libraries(ascii-chat-static-lib INTERFACE ${ZSTD_LIBRARIES})
    target_link_libraries(ascii-chat-static-lib INTERFACE ${PORTAUDIO_LIBRARIES})

    if(APPLE)
        target_link_libraries(ascii-chat-static-lib INTERFACE
            ${COREAUDIO_FRAMEWORK}
            ${AUDIOUNIT_FRAMEWORK}
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${CORESERVICES_FRAMEWORK}
        )
    elseif(UNIX AND NOT USE_MUSL)
        if(JACK_LIBRARY)
            target_link_libraries(ascii-chat-static-lib INTERFACE ${JACK_LIBRARY})
        endif()
    endif()

    if(MIMALLOC_LIBRARIES)
        target_link_libraries(ascii-chat-static-lib INTERFACE ${MIMALLOC_LIBRARIES})
    endif()

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
        target_link_libraries(ascii-chat-static-lib INTERFACE m)
    endif()

    # Symbol Validation for Combined Static Library
    if(ASCIICHAT_LLVM_NM_EXECUTABLE)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a.validated
            COMMAND ${CMAKE_COMMAND}
                -DLLVM_NM=${ASCIICHAT_LLVM_NM_EXECUTABLE}
                -DLIBRARY=${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
                -DSYMBOLS=${ASCIICHAT_SYMBOLS_CSV}
                -P ${CMAKE_SOURCE_DIR}/cmake/utils/ValidateSymbols.cmake
            COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a.validated
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
            COMMENT "Validating ascii-chat symbols in libasciichat.a"
            VERBATIM
        )

        add_custom_target(static-lib
            COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
            COMMAND_ECHO NONE
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a.validated build-timer-start
            VERBATIM
        )
    else()
        message(STATUS "Symbol validation: ${BoldYellow}disabled${ColorReset} (llvm-nm not found)")
        add_custom_target(static-lib
            COMMAND ${CMAKE_COMMAND} -DACTION=check -DTARGET_NAME=static-lib -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
            COMMAND_ECHO NONE
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a build-timer-start
            VERBATIM
        )
    endif()

    # Build target for Release builds (referenced by Executables.cmake)
    add_custom_target(ascii-chat-static-build
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lib/libasciichat.a
        VERBATIM
    )
endif() # NOT BUILDING_OBJECT_LIBS

# =============================================================================
# Unified Library (ascii-chat-static for backward compatibility)
# =============================================================================
if((CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "Dev") AND NOT USE_MUSL)
    # Use the existing ascii-chat-shared target but make it build by default
    set_target_properties(ascii-chat-shared PROPERTIES EXCLUDE_FROM_ALL FALSE)

    add_library(ascii-chat-static INTERFACE)
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-shared)

    set(ASCII_CHAT_UNIFIED_BUILD_TARGET ascii-chat-shared)
else()
    # Release builds or USE_MUSL: ascii-chat-static wraps the combined static library
    add_library(ascii-chat-static INTERFACE)
    target_link_libraries(ascii-chat-static INTERFACE ascii-chat-static-lib)

    set(ASCII_CHAT_UNIFIED_BUILD_TARGET ascii-chat-static-build)
endif()
