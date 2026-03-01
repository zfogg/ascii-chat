# =============================================================================
# FreeType2 Dependency
# =============================================================================
# Cross-platform configuration for FreeType2 font rasterization
#
# For musl builds: Built from source
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - FREETYPE_LIBRARIES: FreeType2 libraries to link
#   - FREETYPE_INCLUDE_DIRS: FreeType2 include directories
# =============================================================================

# Musl build: Build from source
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}freetype${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(FREETYPE_PREFIX "${MUSL_DEPS_DIR_STATIC}/freetype")
    set(FREETYPE_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/freetype-build")

    if(NOT EXISTS "${FREETYPE_PREFIX}/lib/libfreetype.a")
        message(STATUS "  freetype library not found in cache, will build from source")
        ExternalProject_Add(freetype-musl
            URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.tar.gz
            URL_HASH SHA256=427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7ffd2ab90c
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${FREETYPE_BUILD_DIR}
            STAMP_DIR ${FREETYPE_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CMAKE_ARGS
                -DCMAKE_TOOLCHAIN_FILE=${MUSL_TOOLCHAIN_FILE}
                -DMUSL_GCC_PATH=${MUSL_GCC}
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                -DCMAKE_INSTALL_PREFIX=${FREETYPE_PREFIX}
                -DCMAKE_BUILD_TYPE=Release
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES=
                -DCMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES=
                -DCMAKE_C_FLAGS=-nostdinc\ -isystem\ /usr/lib/musl/include\ -O2\ -fPIC
                -DFT_DISABLE_PNG=ON
                -DFT_DISABLE_ZLIB=ON
                -DFT_DISABLE_BZIP2=ON
                -DFT_DISABLE_HARFBUZZ=ON
                -DFT_DISABLE_BROTLI=ON
            BUILD_BYPRODUCTS ${FREETYPE_PREFIX}/lib/libfreetype.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}freetype${ColorReset} library found in cache: ${BoldMagenta}${FREETYPE_PREFIX}/lib/libfreetype.a${ColorReset}")
        add_custom_target(freetype-musl)
    endif()

    set(FREETYPE_LIBRARIES "${FREETYPE_PREFIX}/lib/libfreetype.a")
    set(FREETYPE_INCLUDE_DIRS "${FREETYPE_PREFIX}/include")
    file(MAKE_DIRECTORY "${FREETYPE_PREFIX}/include" "${FREETYPE_PREFIX}/lib")

    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(Freetype REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: ${FREETYPE_LIBRARIES}")

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(Freetype REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: ${FREETYPE_LIBRARIES}")

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(freetype CONFIG REQUIRED)

    message(STATUS "${BoldGreen}✓${ColorReset} FreeType2: vcpkg")

else()
    message(FATAL_ERROR "Unsupported platform for FreeType2")
endif()
