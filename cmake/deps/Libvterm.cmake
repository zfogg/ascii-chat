# =============================================================================
# libvterm Dependency
# =============================================================================
# Cross-platform configuration for libvterm terminal emulation
#
# For WASM/Emscripten: Built from source with Emscripten toolchain
# For musl builds: libvterm is built from source
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - VTERM_LDFLAGS: libvterm libraries to link
#   - VTERM_INCLUDE_DIRS: libvterm include directories
#   - RENDER_FILE_LIBS: Combined libraries for render-file backend
#   - RENDER_FILE_INCLUDES: Combined include directories for render-file backend
#   - GHOSTTY_LIBS: Backwards compatibility alias for RENDER_FILE_LIBS
#   - GHOSTTY_INCLUDES: Backwards compatibility alias for RENDER_FILE_INCLUDES
# =============================================================================
# NOTE: FreeType2 and Fontconfig must be included by Dependencies.cmake before this file

include(FetchContent)

# Shared source URL for all builds (GIT_REPOSITORY + GIT_TAG)
FetchContent_Declare(libvterm-src
    GIT_REPOSITORY https://github.com/neovim/libvterm.git
    GIT_TAG v0.3.3
    SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/libvterm-src"
    UPDATE_DISCONNECTED ON
)

# All builds: Try to find libvterm

# iOS builds: Build from source (only if executables enabled, render-file needs it)
if(PLATFORM_IOS AND BUILD_EXECUTABLES)
    message(STATUS "Configuring ${BoldBlue}libvterm${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)

    set(VTERM_PREFIX "${IOS_DEPS_CACHE_DIR}/libvterm")
    set(VTERM_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/libvterm-build")

    # Determine iOS SDK path
    if(BUILD_IOS_SIM)
        set(IOS_SDK_PATH "$(xcrun --sdk iphonesimulator --show-sdk-path)")
    else()
        set(IOS_SDK_PATH "$(xcrun --sdk iphoneos --show-sdk-path)")
    endif()

    if(NOT EXISTS "${VTERM_PREFIX}/lib/libvterm.a")
        message(STATUS "  libvterm library not found in cache, will build from source")

        ExternalProject_Add(libvterm-ios
            GIT_REPOSITORY https://github.com/neovim/libvterm.git
            GIT_TAG v0.3.3
            UPDATE_DISCONNECTED 1
            PREFIX ${VTERM_BUILD_DIR}
            STAMP_DIR ${VTERM_BUILD_DIR}/stamps
            SOURCE_DIR ${VTERM_BUILD_DIR}/src/libvterm-ios
            BINARY_DIR ${VTERM_BUILD_DIR}/src/libvterm-ios
            BUILD_ALWAYS 0
            DEPENDS freetype-ios
            CONFIGURE_COMMAND ""
            BUILD_COMMAND bash -c "cd <SOURCE_DIR> && make CC=clang CFLAGS='-O2 -fPIC -isysroot ${IOS_SDK_PATH} -arch arm64 -miphoneos-version-min=16.0' LDFLAGS='-isysroot ${IOS_SDK_PATH} -arch arm64' ARFLAGS=rcs"
            INSTALL_COMMAND bash -c "cd <SOURCE_DIR> && make install PREFIX=${VTERM_PREFIX}"
            BUILD_BYPRODUCTS ${VTERM_PREFIX}/lib/libvterm.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libvterm${ColorReset} library found in cache: ${BoldMagenta}${VTERM_PREFIX}/lib/libvterm.a${ColorReset}")
        add_custom_target(libvterm-ios)
    endif()

    set(VTERM_LDFLAGS "${VTERM_PREFIX}/lib/libvterm.a")
    set(VTERM_INCLUDE_DIRS "${VTERM_PREFIX}/include")
    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend (iOS): ${BoldCyan}libvterm + FreeType2${ColorReset}")

    return()
endif()

# iOS without executables: Skip libvterm (not needed, render-file is disabled)
if(PLATFORM_IOS)
    message(STATUS "Skipping libvterm (iOS with BUILD_EXECUTABLES=OFF)")
    return()
endif()

# WASM builds: Build libvterm from source using Emscripten
if(DEFINED EMSCRIPTEN)
    message(STATUS "Configuring ${BoldBlue}libvterm${ColorReset} from source (WASM)...")

    # For WASM, the libvterm source is in the build-type-specific cache where FetchContent downloaded it
    # during the main build configuration
    set(libvterm_wasm_SOURCE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libvterm-src")

    # If source doesn't exist at expected location, try to use the one from FetchContent
    if(NOT EXISTS "${libvterm_wasm_SOURCE_DIR}/src")
        message(STATUS "  libvterm source not found in ${libvterm_wasm_SOURCE_DIR}")
        # Try the regular FetchContent location
        FetchContent_MakeAvailable(libvterm-src)
        set(libvterm_wasm_SOURCE_DIR "${libvterm-src_SOURCE_DIR}")
        if(NOT EXISTS "${libvterm_wasm_SOURCE_DIR}/src")
            message(FATAL_ERROR "libvterm source not found after FetchContent_Populate")
        endif()
    endif()

    # Generate encoding .inc files from .tbl files
    file(GLOB TBL_FILES "${libvterm_wasm_SOURCE_DIR}/src/encoding/*.tbl")
    foreach(tbl ${TBL_FILES})
        get_filename_component(tbl_name ${tbl} NAME_WE)
        set(inc_file "${libvterm_wasm_SOURCE_DIR}/src/encoding/${tbl_name}.inc")
        if(NOT EXISTS "${inc_file}")
            execute_process(
                COMMAND perl -CSD "${libvterm_wasm_SOURCE_DIR}/tbl2inc_c.pl" "${tbl}"
                OUTPUT_FILE "${inc_file}"
                RESULT_VARIABLE TBL_RESULT
            )
            if(NOT TBL_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to generate ${inc_file} from ${tbl}")
            endif()
        endif()
    endforeach()

    # Compile all source files using emcc with WASM-aware flags
    # These flags must match those used in src/web/CMakeLists.txt to ensure
    # function table type signatures are compatible when libvterm is linked into the main module
    file(GLOB C_FILES "${libvterm_wasm_SOURCE_DIR}/src/*.c")
    set(OBJ_FILES "")
    foreach(src ${C_FILES})
        get_filename_component(name ${src} NAME_WE)
        set(obj "${libvterm_wasm_SOURCE_DIR}/${name}.o")
        execute_process(
            COMMAND emcc -O3 -fPIC -Wno-error
                -I${libvterm_wasm_SOURCE_DIR}/include -std=c99
                -msimd128
                -c ${src} -o ${obj}
            RESULT_VARIABLE CC_RESULT
        )
        if(NOT CC_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to compile ${src}")
        endif()
        list(APPEND OBJ_FILES ${obj})
    endforeach()

    # Create static archive using emar
    execute_process(
        COMMAND emar rcs "${libvterm_wasm_SOURCE_DIR}/libvterm.a" ${OBJ_FILES}
        RESULT_VARIABLE AR_RESULT
    )
    if(NOT AR_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to create libvterm.a")
    endif()

    set(VTERM_LDFLAGS "${libvterm_wasm_SOURCE_DIR}/libvterm.a")
    set(VTERM_INCLUDE_DIRS "${libvterm_wasm_SOURCE_DIR}/include")

    message(STATUS "${BoldGreen}✓${ColorReset} WASM: ${BoldCyan}libvterm${ColorReset}")

    return()
endif()

# musl builds: Build from source
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}libvterm${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(VTERM_PREFIX "${MUSL_DEPS_DIR_STATIC}/libvterm")
    set(VTERM_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/libvterm-build")

    if(NOT EXISTS "${VTERM_PREFIX}/lib/libvterm.a")
        message(STATUS "  libvterm library not found in cache, will build from source")

        ExternalProject_Add(libvterm-musl
            GIT_REPOSITORY https://github.com/neovim/libvterm.git
            GIT_TAG v0.3.3
            UPDATE_DISCONNECTED 1
            PREFIX ${VTERM_BUILD_DIR}
            STAMP_DIR ${VTERM_BUILD_DIR}/stamps
            SOURCE_DIR ${VTERM_BUILD_DIR}/src/libvterm-musl
            BINARY_DIR ${VTERM_BUILD_DIR}/src/libvterm-musl
            BUILD_ALWAYS 0
            DEPENDS freetype-musl fontconfig
            CONFIGURE_COMMAND ""
            BUILD_COMMAND ${CMAKE_COMMAND} -DMUSL_GCC=${MUSL_GCC} -DKERNEL_HEADERS_DIR=${KERNEL_HEADERS_DIR} -DSOURCE_DIR=${VTERM_BUILD_DIR}/src/libvterm-musl -P ${CMAKE_SOURCE_DIR}/cmake/scripts/build-libvterm.cmake
            INSTALL_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${VTERM_BUILD_DIR}/src/libvterm-musl -DPREFIX=${VTERM_PREFIX} -P ${CMAKE_SOURCE_DIR}/cmake/scripts/install-libvterm.cmake
            BUILD_BYPRODUCTS ${VTERM_PREFIX}/lib/libvterm.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}libvterm${ColorReset} library found in cache: ${BoldMagenta}${VTERM_PREFIX}/lib/libvterm.a${ColorReset}")
        add_custom_target(libvterm-musl)
    endif()

    set(VTERM_LDFLAGS "${VTERM_PREFIX}/lib/libvterm.a")
    set(VTERM_INCLUDE_DIRS "${VTERM_PREFIX}/include")
    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})
    set(VTERM_MUSL_TARGET "libvterm-musl")

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

# Unix builds (Linux/BSD, non-musl): Use system package manager
elseif(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)

    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

elseif(APPLE)
    # macOS: Use system package managers (homebrew or macports)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM vterm REQUIRED)

    set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
    set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2 + fontconfig${ColorReset}")

elseif(WIN32)
    # Windows: Use vcpkg for FreeType; build libvterm from source

    set(VTERM_PREFIX "${ASCIICHAT_DEPS_CACHE_DIR}/libvterm")
    set(VTERM_SOURCE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/libvterm-src")
    set(VTERM_LIB "${VTERM_PREFIX}/lib/vterm.lib")

    if(NOT EXISTS "${VTERM_LIB}")
        message(STATUS "${BoldYellow}libvterm${ColorReset} not found in cache, building from source...")

        # Clone libvterm source
        if(NOT EXISTS "${VTERM_SOURCE_DIR}/src")
            execute_process(
                COMMAND git clone --depth 1 --branch v0.3.3 https://github.com/neovim/libvterm.git "${VTERM_SOURCE_DIR}"
                RESULT_VARIABLE CLONE_RESULT
            )
            if(NOT CLONE_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to clone libvterm")
            endif()
        endif()

        file(MAKE_DIRECTORY "${VTERM_PREFIX}/lib" "${VTERM_PREFIX}/include")

        # Generate encoding .inc files from .tbl files
        find_program(PERL_EXECUTABLE perl)
        if(PERL_EXECUTABLE)
            file(GLOB TBL_FILES "${VTERM_SOURCE_DIR}/src/encoding/*.tbl")
            foreach(tbl ${TBL_FILES})
                get_filename_component(tbl_name ${tbl} NAME_WE)
                set(inc_file "${VTERM_SOURCE_DIR}/src/encoding/${tbl_name}.inc")
                if(NOT EXISTS "${inc_file}")
                    execute_process(
                        COMMAND "${PERL_EXECUTABLE}" -CSD "${VTERM_SOURCE_DIR}/tbl2inc_c.pl" "${tbl}"
                        OUTPUT_FILE "${inc_file}"
                        RESULT_VARIABLE TBL_RESULT
                    )
                    if(NOT TBL_RESULT EQUAL 0)
                        message(WARNING "Failed to generate ${inc_file} - encoding tables may be missing")
                    endif()
                endif()
            endforeach()
        else()
            message(STATUS "  Perl not found - using pre-generated encoding tables if available")
        endif()

        # Compile all source files with clang
        file(GLOB C_FILES "${VTERM_SOURCE_DIR}/src/*.c")
        set(OBJ_FILES "")
        foreach(src ${C_FILES})
            get_filename_component(name ${src} NAME_WE)
            set(obj "${VTERM_SOURCE_DIR}/${name}.obj")
            execute_process(
                COMMAND "${CMAKE_C_COMPILER}" -O2 -w
                    -I "${VTERM_SOURCE_DIR}/include" -std=c99
                    -c "${src}" -o "${obj}"
                RESULT_VARIABLE CC_RESULT
            )
            if(NOT CC_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to compile libvterm ${src}")
            endif()
            list(APPEND OBJ_FILES "${obj}")
        endforeach()

        # Create static archive with llvm-lib
        execute_process(
            COMMAND "${CMAKE_AR}" rcs "${VTERM_LIB}" ${OBJ_FILES}
            RESULT_VARIABLE AR_RESULT
        )
        if(NOT AR_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to create vterm.lib")
        endif()

        # Copy headers
        file(COPY "${VTERM_SOURCE_DIR}/include/" DESTINATION "${VTERM_PREFIX}/include")

        message(STATUS "  ${BoldGreen}libvterm${ColorReset} built and cached successfully")
    else()
        message(STATUS "  ${BoldBlue}libvterm${ColorReset} library found in cache: ${BoldMagenta}${VTERM_LIB}${ColorReset}")
    endif()

    # Create imported target
    add_library(vterm STATIC IMPORTED GLOBAL)
    set_target_properties(vterm PROPERTIES
        IMPORTED_LOCATION "${VTERM_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${VTERM_PREFIX}/include"
    )

    set(VTERM_LDFLAGS vterm)
    set(VTERM_INCLUDE_DIRS "${VTERM_PREFIX}/include")
    set(RENDER_FILE_LIBS vterm ${FREETYPE_LIBRARIES})
    set(RENDER_FILE_INCLUDES "${VTERM_PREFIX}/include" ${FREETYPE_INCLUDE_DIRS})

    message(STATUS "${BoldGreen}✓${ColorReset} Render-file backend: ${BoldCyan}libvterm + FreeType2${ColorReset} (no fontconfig on Windows)")

else()
    message(FATAL_ERROR "Unsupported platform for render-file backend")
endif()

# =============================================================================
# Backwards compatibility: Set GHOSTTY_* variables for legacy code
# =============================================================================

set(GHOSTTY_LIBS ${RENDER_FILE_LIBS})
set(GHOSTTY_INCLUDES ${RENDER_FILE_INCLUDES})
