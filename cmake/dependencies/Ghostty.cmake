# =============================================================================
# Ghostty Terminal Emulator Configuration
# =============================================================================
# Configures ghostty (libghostty) for render-to-file functionality
#
# Ghostty is used for:
#   - macOS: Cross-platform ASCII rendering via Metal backend
#   - Linux: Font measurement via libvterm + FreeType2 + fontconfig
#   - Windows: Stubs only
#
# Build strategy (macOS only):
#   1. Check for system-installed ghostty (e.g., from package manager)
#   2. Build from submodule if system install not found
#   3. Cache built library for reuse across clean builds
#
# Outputs (variables set by this file):
#   - GHOSTTY_FOUND: Whether ghostty was found or built successfully (macOS only)
#   - GHOSTTY_LIBRARIES: Libraries to link against (ghostty_lib target)
#   - GHOSTTY_INCLUDE_DIRS: Include directories
#   - GHOSTTY_LIBS: Render backend libraries for all platforms
#   - GHOSTTY_INCLUDES: Render backend include dirs for all platforms
# =============================================================================

include(ProcessorCount)
ProcessorCount(NPROC)
if(NPROC EQUAL 0)
    set(NPROC 1)
endif()

# =============================================================================
# macOS: Build/find ghostty library
# =============================================================================

if(APPLE)
    # Try to find ghostty (system install or package manager)
    find_library(GHOSTTY_SYSTEM_LIB NAMES ghostty
                 PATHS /usr/local/lib /usr/lib /opt/homebrew/lib
                 NO_DEFAULT_PATH)
    find_path(GHOSTTY_SYSTEM_INC NAMES ghostty.h
              PATHS /usr/local/include /usr/include /opt/homebrew/include
              NO_DEFAULT_PATH)

    if(GHOSTTY_SYSTEM_LIB AND GHOSTTY_SYSTEM_INC)
        # Use system-installed ghostty
        add_library(ghostty_lib STATIC IMPORTED)
        set_target_properties(ghostty_lib PROPERTIES
            IMPORTED_LOCATION "${GHOSTTY_SYSTEM_LIB}"
        )
        target_include_directories(ghostty_lib INTERFACE "${GHOSTTY_SYSTEM_INC}")
        set(GHOSTTY_LIBRARIES ghostty_lib)
        set(GHOSTTY_INCLUDE_DIRS "${GHOSTTY_SYSTEM_INC}")
        set(GHOSTTY_FOUND TRUE)

        message(STATUS "Using system ${BoldGreen}ghostty${ColorReset} library: ${GHOSTTY_SYSTEM_LIB}")

    # Fall back to building from submodule
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
        set(GHOSTTY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
        set(GHOSTTY_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/ghostty")
        set(GHOSTTY_LIB "${GHOSTTY_BUILD_DIR}/libghostty.a")

        file(MAKE_DIRECTORY "${GHOSTTY_BUILD_DIR}")

        # Only build if library doesn't exist in cache
        if(NOT EXISTS "${GHOSTTY_LIB}")
            message(STATUS "${BoldYellow}ghostty${ColorReset} library not found in cache, building from source...")

            # Check for zig compiler (required to build ghostty)
            find_program(ZIG_EXECUTABLE NAMES zig)
            if(NOT ZIG_EXECUTABLE)
                message(FATAL_ERROR "${BoldRed}zig compiler not found${ColorReset}. ghostty requires Zig to build.\n"
                                  "Install from: https://ziglang.org/download/")
            endif()

            # Build ghostty using zig build (full app with Metal backend, embedded runtime for C API)
            set(GHOSTTY_LOG_FILE "${GHOSTTY_BUILD_DIR}/ghostty-build.log")
            execute_process(
                COMMAND "${ZIG_EXECUTABLE}" build -Dapp-runtime=embedded -Doptimize=ReleaseFast --prefix "${GHOSTTY_BUILD_DIR}"
                WORKING_DIRECTORY "${GHOSTTY_SOURCE_DIR}"
                RESULT_VARIABLE GHOSTTY_BUILD_RESULT
                OUTPUT_FILE "${GHOSTTY_LOG_FILE}"
                ERROR_FILE "${GHOSTTY_LOG_FILE}"
            )

            if(NOT GHOSTTY_BUILD_RESULT EQUAL 0)
                message(FATAL_ERROR "${BoldRed}ghostty build failed${ColorReset}. Check log: ${GHOSTTY_LOG_FILE}")
            endif()

            # Copy built library to cache
            find_file(GHOSTTY_BUILT_LIB NAMES "libghostty.a"
                      PATHS "${GHOSTTY_BUILD_DIR}/lib"
                      HINTS "${GHOSTTY_SOURCE_DIR}/zig-out/lib"
                      NO_DEFAULT_PATH)

            if(GHOSTTY_BUILT_LIB)
                file(COPY_FILE "${GHOSTTY_BUILT_LIB}" "${GHOSTTY_LIB}")
                message(STATUS "  ${BoldGreen}ghostty${ColorReset} library built and cached successfully")
            else()
                message(FATAL_ERROR "${BoldRed}ghostty library not found${ColorReset} in build output")
            endif()

            # Create a dummy target so dependencies work
            add_custom_target(ghostty_build)
        else()
            message(STATUS "${BoldGreen}ghostty${ColorReset} library found in cache: ${BoldCyan}${GHOSTTY_LIB}${ColorReset}")
            add_custom_target(ghostty_build)
        endif()

        # Create an imported library that links to the built library
        add_library(ghostty_lib STATIC IMPORTED GLOBAL)
        set_target_properties(ghostty_lib PROPERTIES
            IMPORTED_LOCATION "${GHOSTTY_LIB}"
        )
        target_include_directories(ghostty_lib INTERFACE
            "${GHOSTTY_SOURCE_DIR}/zig-out/include"
        )
        add_dependencies(ghostty_lib ghostty_build)

        set(GHOSTTY_LIBRARIES ghostty_lib)
        set(GHOSTTY_INCLUDE_DIRS "${GHOSTTY_SOURCE_DIR}/zig-out/include")
        set(GHOSTTY_FOUND TRUE)

        message(STATUS "${BoldGreen}ghostty${ColorReset} configured: ${GHOSTTY_LIB}")

    else()
        message(STATUS "${BoldYellow}ghostty submodule not found${ColorReset} - Ghostty macOS support will be limited")
        set(GHOSTTY_FOUND FALSE)
        set(GHOSTTY_LIBRARIES "")
        set(GHOSTTY_INCLUDE_DIRS "")
    endif()
elseif(UNIX AND NOT APPLE)
    # Linux/BSD: Build ghostty for pixel rendering (similar to macOS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
        set(GHOSTTY_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
        set(GHOSTTY_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/ghostty")
        set(GHOSTTY_LIB "${GHOSTTY_BUILD_DIR}/libghostty.a")

        file(MAKE_DIRECTORY "${GHOSTTY_BUILD_DIR}")

        # Only build if library doesn't exist in cache
        if(NOT EXISTS "${GHOSTTY_LIB}")
            message(STATUS "${BoldYellow}ghostty${ColorReset} library not found in cache, building from source...")

            # Check for zig compiler (required to build ghostty)
            find_program(ZIG_EXECUTABLE NAMES zig)
            if(NOT ZIG_EXECUTABLE)
                message(FATAL_ERROR "${BoldRed}zig compiler not found${ColorReset}. ghostty requires Zig to build.\n"
                                  "Install from: https://ziglang.org/download/")
            endif()

            # Build full ghostty library using zig build
            # Use pkg-config to get proper include paths for GTK/glib dependencies
            set(GHOSTTY_LOG_FILE "${GHOSTTY_BUILD_DIR}/ghostty-build.log")

            # Get GTK4 and libadwaita include paths using pkg-config
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(GTK4 gtk4)
            pkg_check_modules(LIBADWAITA libadwaita-1)
            pkg_check_modules(GRAPHENE graphene-gobject-1.0)

            # Build CFLAGS from pkg-config results
            set(GHOSTTY_CFLAGS "")
            if(GTK4_FOUND)
                foreach(include_dir ${GTK4_INCLUDE_DIRS})
                    string(APPEND GHOSTTY_CFLAGS " -isystem ${include_dir}")
                endforeach()
            endif()
            if(LIBADWAITA_FOUND)
                foreach(include_dir ${LIBADWAITA_INCLUDE_DIRS})
                    string(APPEND GHOSTTY_CFLAGS " -isystem ${include_dir}")
                endforeach()
            endif()
            foreach(include_dir ${GRAPHENE_INCLUDE_DIRS})
                string(APPEND GHOSTTY_CFLAGS " -isystem ${include_dir}")
            endforeach()

            message(STATUS "Building ghostty library with CFLAGS:${GHOSTTY_CFLAGS}")
            message(STATUS "Working directory: ${GHOSTTY_SOURCE_DIR}")

            # Build search-prefix list for existing directories
            set(SEARCH_PREFIXES "")
            foreach(PREFIX "/usr" "/usr/local" "/home/linuxbrew/.linuxbrew" "/opt/homebrew")
                if(EXISTS "${PREFIX}")
                    list(APPEND SEARCH_PREFIXES "--search-prefix" "${PREFIX}")
                endif()
            endforeach()

            execute_process(
                COMMAND env CFLAGS="${GHOSTTY_CFLAGS}" CXXFLAGS="${GHOSTTY_CFLAGS}" PKG_CONFIG_PATH="/usr/lib/pkgconfig:/usr/share/pkgconfig:/home/linuxbrew/.linuxbrew/lib/pkgconfig:/home/linuxbrew/.linuxbrew/share/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/share/pkgconfig:/opt/homebrew/lib/pkgconfig:/opt/homebrew/share/pkgconfig" "${ZIG_EXECUTABLE}" build install -Dapp-runtime=gtk -Doptimize=ReleaseFast ${SEARCH_PREFIXES} --prefix "${GHOSTTY_BUILD_DIR}"
                WORKING_DIRECTORY "${GHOSTTY_SOURCE_DIR}"
                RESULT_VARIABLE GHOSTTY_BUILD_RESULT
                OUTPUT_FILE "${GHOSTTY_LOG_FILE}"
                ERROR_FILE "${GHOSTTY_LOG_FILE}"
            )
            message(STATUS "Build result code: ${GHOSTTY_BUILD_RESULT}")

            if(GHOSTTY_BUILD_RESULT EQUAL 0)
                # Ghostty build succeeded, find and copy library to cache location
                # Look for libghostty (the full library)
                find_file(GHOSTTY_FULL_LIB NAMES "libghostty.a" "libghostty.so" "libghostty.so.0"
                          PATHS "${GHOSTTY_BUILD_DIR}/lib"
                          NO_DEFAULT_PATH)
                if(GHOSTTY_FULL_LIB)
                    file(COPY_FILE "${GHOSTTY_FULL_LIB}" "${GHOSTTY_LIB}")
                    message(STATUS "Copied ${GHOSTTY_FULL_LIB} to ${GHOSTTY_LIB}")
                else()
                    message(STATUS "Warning: libghostty not found in ${GHOSTTY_BUILD_DIR}/lib")
                    # Try to find lib-vt as fallback
                    find_file(GHOSTTY_VT_LIB NAMES "libghostty-vt.a" "libghostty-vt.so.0" "libghostty-vt.so"
                              PATHS "${GHOSTTY_BUILD_DIR}/lib"
                              NO_DEFAULT_PATH)
                    if(GHOSTTY_VT_LIB)
                        file(COPY_FILE "${GHOSTTY_VT_LIB}" "${GHOSTTY_LIB}")
                        message(STATUS "Fallback: Copied ${GHOSTTY_VT_LIB} to ${GHOSTTY_LIB}")
                    else()
                        set(GHOSTTY_BUILD_RESULT 1)
                    endif()
                endif()

                # Copy ghostty.h from source to cache so it's available for compilation
                if(GHOSTTY_BUILD_RESULT EQUAL 0)
                    set(GHOSTTY_HEADER_SRC "${GHOSTTY_SOURCE_DIR}/include/ghostty.h")
                    set(GHOSTTY_HEADER_DST "${GHOSTTY_BUILD_DIR}/include/ghostty.h")
                    if(EXISTS "${GHOSTTY_HEADER_SRC}")
                        file(MAKE_DIRECTORY "${GHOSTTY_BUILD_DIR}/include")
                        file(COPY_FILE "${GHOSTTY_HEADER_SRC}" "${GHOSTTY_HEADER_DST}")
                        message(STATUS "Cached ghostty.h header")
                    endif()
                endif()
            endif()

            if(NOT GHOSTTY_BUILD_RESULT EQUAL 0)
                message(WARNING "${BoldYellow}ghostty lib-vt build failed${ColorReset}. Check log: ${GHOSTTY_LOG_FILE}")
                message(STATUS "${BoldYellow}Continuing without ghostty...${ColorReset}")
                set(GHOSTTY_FOUND FALSE)
            endif()

            message(STATUS "  ${BoldGreen}ghostty lib-vt${ColorReset} library built and cached successfully")
            add_custom_target(ghostty_build)
        else()
            message(STATUS "${BoldGreen}ghostty${ColorReset} library found in cache: ${BoldCyan}${GHOSTTY_LIB}${ColorReset}")
            add_custom_target(ghostty_build)
        endif()

        # Create an imported library that links to the built library (only if build succeeded)
        if(EXISTS "${GHOSTTY_LIB}")
            add_library(ghostty_lib STATIC IMPORTED GLOBAL)
            set_target_properties(ghostty_lib PROPERTIES
                IMPORTED_LOCATION "${GHOSTTY_LIB}"
            )
            target_include_directories(ghostty_lib INTERFACE
                "${GHOSTTY_BUILD_DIR}/include"
            )
            add_dependencies(ghostty_lib ghostty_build)
        else()
            # Create stub library if build failed
            set(GHOSTTY_FOUND FALSE)
        endif()

        # Link ghostty's dependencies (only if library was created)
        # libghostty built as static archive contains symbols from its dependencies
        # These must be available when linking the final executable
        if(TARGET ghostty_lib)
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(ONIGURUMA QUIET oniguruma)
                if(ONIGURUMA_FOUND)
                    target_link_libraries(ghostty_lib INTERFACE ${ONIGURUMA_LIBRARIES})
                else()
                    # Fallback if pkg-config doesn't find oniguruma
                    target_link_libraries(ghostty_lib INTERFACE onig)
                endif()
            else()
                target_link_libraries(ghostty_lib INTERFACE onig)
            endif()

            set(GHOSTTY_LIBRARIES ghostty_lib)
            set(GHOSTTY_INCLUDE_DIRS "${GHOSTTY_BUILD_DIR}/include")
            set(GHOSTTY_FOUND TRUE)
        else()
            set(GHOSTTY_LIBRARIES "")
            set(GHOSTTY_INCLUDE_DIRS "")
            set(GHOSTTY_FOUND FALSE)
        endif()

        message(STATUS "${BoldGreen}ghostty${ColorReset} configured: ${GHOSTTY_LIB}")

    else()
        message(STATUS "${BoldYellow}ghostty submodule not found${ColorReset} - Ghostty Linux support will be limited")
        set(GHOSTTY_FOUND FALSE)
        set(GHOSTTY_LIBRARIES "")
        set(GHOSTTY_INCLUDE_DIRS "")
    endif()

    # Custom target to cache ghostty.h header (Linux)
    if(UNIX AND NOT APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
        set(GHOSTTY_EMBEDDED_SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ghostty")
        set(GHOSTTY_EMBEDDED_BUILD_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/ghostty")
        set(GHOSTTY_EMBEDDED_HEADER_SRC "${GHOSTTY_EMBEDDED_SOURCE_DIR}/include/ghostty.h")
        set(GHOSTTY_EMBEDDED_HEADER_DST "${GHOSTTY_EMBEDDED_BUILD_DIR}/include/ghostty.h")

        add_custom_target(ghostty-embedded
            COMMAND mkdir -p "${GHOSTTY_EMBEDDED_BUILD_DIR}/include"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GHOSTTY_EMBEDDED_HEADER_SRC}" "${GHOSTTY_EMBEDDED_HEADER_DST}"
            COMMAND ${CMAKE_COMMAND} -E echo "✓ ghostty.h cached at ${GHOSTTY_EMBEDDED_HEADER_DST}"
            VERBATIM
        )
    endif()
else()
    # Windows: ghostty not used for rendering
    set(GHOSTTY_FOUND FALSE)
    set(GHOSTTY_LIBRARIES "")
    set(GHOSTTY_INCLUDE_DIRS "")
endif()

# =============================================================================
# Bundled font setup (at configure time) - shared across all render backends
# =============================================================================

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/fonts")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/data/fonts")

set(MATRIX_FONT_URL "https://github.com/Rezmason/matrix/raw/master/assets/Matrix-Resurrected.ttf")
set(MATRIX_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/Matrix-Resurrected.ttf")
set(MATRIX_FONT_GEN "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c")

# Download font at configure time
if(NOT EXISTS "${MATRIX_FONT_SRC}")
    message(STATUS "Downloading Matrix-Resurrected.ttf...")
    file(DOWNLOAD "${MATRIX_FONT_URL}" "${MATRIX_FONT_SRC}" SHOW_PROGRESS TLS_VERIFY ON)
endif()

# Convert to C array at configure time
if(NOT EXISTS "${MATRIX_FONT_GEN}" OR "${MATRIX_FONT_SRC}" IS_NEWER_THAN "${MATRIX_FONT_GEN}")
    message(STATUS "Embedding Matrix-Resurrected.ttf as C array...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${MATRIX_FONT_SRC}"
                "-DOUTPUT=${MATRIX_FONT_GEN}"
                "-DVAR_NAME=g_font_matrix_resurrected"
                "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
        RESULT_VARIABLE BIN2C_RESULT
    )
    if(NOT BIN2C_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to convert font to C array")
    endif()
endif()

# Download and embed a default monospace font (DejaVu Sans Mono) for fallback
# This ensures render-file works even if system fonts are unavailable
set(DEFAULT_FONT_TARBALL_URL "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2")
set(DEFAULT_FONT_TARBALL "${CMAKE_BINARY_DIR}/fonts/dejavu-fonts-ttf-2.37.tar.bz2")
set(DEFAULT_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/DejaVuSansMono.ttf")
set(DEFAULT_FONT_GEN "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c")

# Download and extract font tarball at configure time
if(NOT EXISTS "${DEFAULT_FONT_SRC}")
    if(NOT EXISTS "${DEFAULT_FONT_TARBALL}")
        message(STATUS "Downloading DejaVu fonts tarball...")
        file(DOWNLOAD "${DEFAULT_FONT_TARBALL_URL}" "${DEFAULT_FONT_TARBALL}"
             SHOW_PROGRESS
             TLS_VERIFY ON
             STATUS DOWNLOAD_STATUS)
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_ERROR)
        if(NOT DOWNLOAD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download DejaVu fonts tarball: ${DOWNLOAD_ERROR}")
        endif()
    endif()

    message(STATUS "Extracting DejaVuSansMono.ttf from tarball...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf "${DEFAULT_FONT_TARBALL}" "dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/fonts"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract DejaVuSansMono.ttf from tarball")
    endif()

    # Move extracted file to the right place
    file(RENAME "${CMAKE_BINARY_DIR}/fonts/dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf" "${DEFAULT_FONT_SRC}")
endif()

# Convert to C array at configure time
if(NOT EXISTS "${DEFAULT_FONT_GEN}" OR "${DEFAULT_FONT_SRC}" IS_NEWER_THAN "${DEFAULT_FONT_GEN}")
    message(STATUS "Embedding DejaVuSansMono.ttf as C array...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${DEFAULT_FONT_SRC}"
                "-DOUTPUT=${DEFAULT_FONT_GEN}"
                "-DVAR_NAME=g_font_default"
                "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
        RESULT_VARIABLE BIN2C_RESULT
    )
    if(NOT BIN2C_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to convert default font to C array")
    endif()
endif()

# =============================================================================
# Render-to-file backend selection (platform-specific)
# =============================================================================

if(WIN32)
    message(STATUS "Ghostty: stubs only (Windows)")
    set(GHOSTTY_LIBS "")
    set(GHOSTTY_INCLUDES "")
elseif(APPLE)
    if(GHOSTTY_FOUND)
        set(GHOSTTY_LIBS ${GHOSTTY_LIBRARIES} "-framework Metal" "-framework Cocoa" "-framework CoreGraphics")
        set(GHOSTTY_INCLUDES ${GHOSTTY_INCLUDE_DIRS})
        message(STATUS "${BoldGreen}✓${ColorReset} Ghostty (macOS): ghostty + Metal")
    else()
        message(WARNING "Ghostty: ghostty not found - macOS renderer will fail at runtime")
        set(GHOSTTY_LIBS "")
        set(GHOSTTY_INCLUDES "")
    endif()
elseif(UNIX AND NOT APPLE)
    # Linux: ghostty with GTK backend for rendering
    if(GHOSTTY_FOUND)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK gtk+-3.0 REQUIRED)

        set(GHOSTTY_LIBS ${GHOSTTY_LIBRARIES} ${GTK_LDFLAGS})
        set(GHOSTTY_INCLUDES ${GHOSTTY_INCLUDE_DIRS} ${GTK_INCLUDE_DIRS})
        message(STATUS "${BoldGreen}✓${ColorReset} Ghostty (Linux): ghostty + GTK")
    else()
        message(WARNING "Ghostty: ghostty not found - Linux renderer will not be available")
        set(GHOSTTY_LIBS "")
        set(GHOSTTY_INCLUDES "")
    endif()
else()
    # Windows: stubs only
    set(GHOSTTY_LIBS "")
    set(GHOSTTY_INCLUDES "")
endif()
