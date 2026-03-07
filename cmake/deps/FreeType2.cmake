# =============================================================================
# FreeType2 Dependency
# =============================================================================
# Cross-platform configuration for FreeType2 font rasterization
#
# For musl builds: Built from source
# For iOS builds: Built from source with iOS cross-compilation
# For native builds: Uses system package manager
#
# Outputs (variables set by this file):
#   - FREETYPE_LIBRARIES: FreeType2 libraries to link
#   - FREETYPE_INCLUDE_DIRS: FreeType2 include directories
# =============================================================================

# iOS build: Build from source for iOS cross-compilation
# Only build FreeType if we're building executables or libvterm (both need fonts)
if(PLATFORM_IOS AND BUILD_EXECUTABLES)
    message(STATUS "Configuring ${BoldBlue}freetype${ColorReset} from source (iOS cross-compile)...")

    include(ExternalProject)

    set(FREETYPE_PREFIX "${IOS_DEPS_CACHE_DIR}/freetype")
    set(FREETYPE_BUILD_DIR "${IOS_DEPS_CACHE_DIR}/freetype-build")

    if(NOT EXISTS "${FREETYPE_PREFIX}/lib/libfreetype.a")
        message(STATUS "  freetype library not found in cache, will build from source")

        # Determine iOS SDK path
        if(BUILD_IOS_SIM)
            set(IOS_SDK_PATH "$(xcrun --sdk iphonesimulator --show-sdk-path)")
        else()
            set(IOS_SDK_PATH "$(xcrun --sdk iphoneos --show-sdk-path)")
        endif()

        ExternalProject_Add(freetype-ios
            URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.tar.gz
            URL_HASH SHA256=427201f5d5151670d05c1f5b45bef5dda1f2e7dd971ef54f0feaaa7ffd2ab90c
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${FREETYPE_BUILD_DIR}
            STAMP_DIR ${FREETYPE_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CMAKE_ARGS
                -DCMAKE_SYSTEM_NAME=iOS
                -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
                -DCMAKE_OSX_ARCHITECTURES=arm64
                -DCMAKE_OSX_SYSROOT=${IOS_SDK_PATH}
                -DCMAKE_POLICY_VERSION_MINIMUM=3.5
                -DCMAKE_INSTALL_PREFIX=${FREETYPE_PREFIX}
                -DCMAKE_BUILD_TYPE=Release
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DFT_DISABLE_PNG=ON
                -DFT_DISABLE_ZLIB=ON
                -DFT_DISABLE_BZIP2=ON
                -DFT_DISABLE_HARFBUZZ=ON
                -DFT_DISABLE_BROTLI=ON
            INSTALL_COMMAND "${CMAKE_COMMAND}" --install . --prefix ${FREETYPE_PREFIX}
            BUILD_BYPRODUCTS ${FREETYPE_PREFIX}/lib/libfreetype.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}freetype${ColorReset} library found in cache: ${BoldMagenta}${FREETYPE_PREFIX}/lib/libfreetype.a${ColorReset}")
        add_custom_target(freetype-ios)
    endif()

    set(FREETYPE_LIBRARIES "${FREETYPE_PREFIX}/lib/libfreetype.a" PARENT_SCOPE)
    set(FREETYPE_INCLUDE_DIRS "${FREETYPE_PREFIX}/include" "${FREETYPE_PREFIX}/include/freetype2" PARENT_SCOPE)
    set(FREETYPE_PREFIX "${FREETYPE_PREFIX}" PARENT_SCOPE)
    file(MAKE_DIRECTORY "${FREETYPE_PREFIX}/include" "${FREETYPE_PREFIX}/lib")

    return()
endif()

# iOS without executables: Skip FreeType (not needed, render-file is disabled)
if(PLATFORM_IOS)
    message(STATUS "Skipping FreeType (iOS with BUILD_EXECUTABLES=OFF)")
    return()
endif()

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
            INSTALL_COMMAND "${CMAKE_COMMAND}" --install . --prefix ${FREETYPE_PREFIX}
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

    set(FREETYPE_LIBRARIES "${FREETYPE_PREFIX}/lib/libfreetype.a" PARENT_SCOPE)
    set(FREETYPE_INCLUDE_DIRS "${FREETYPE_PREFIX}/include" "${FREETYPE_PREFIX}/include/freetype2" PARENT_SCOPE)
    set(FREETYPE_PREFIX "${FREETYPE_PREFIX}" PARENT_SCOPE)
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
