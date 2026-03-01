# =============================================================================
# Abseil-cpp (Google C++ Library) Dependency
# =============================================================================
# Configuration for Abseil-cpp, Google's C++ library used by WebRTC dependencies
#
# For musl builds: Built from source
# For native builds: Uses system package manager or builds from source
#
# Outputs (variables set by this file):
#   - ABSEIL_LIBRARIES: Abseil libraries to link (PARENT_SCOPE)
#   - ABSEIL_INCLUDE_DIRS: Abseil include directories (PARENT_SCOPE)
# =============================================================================

# Musl build: Build from source
if(USE_MUSL)
    message(STATUS "Configuring ${BoldBlue}Abseil-cpp${ColorReset} from source (musl)...")

    include(ExternalProject)

    set(ABSEIL_PREFIX "${MUSL_DEPS_DIR_STATIC}/abseil")
    set(ABSEIL_BUILD_DIR "${MUSL_DEPS_DIR_STATIC}/abseil-build")

    # Only add external project if library doesn't exist
    if(NOT EXISTS "${ABSEIL_PREFIX}/lib/libabsl_base.a")
        message(STATUS "  Abseil library not found in cache, will build from source")
        ExternalProject_Add(abseil-musl
            URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.1.tar.gz
            URL_HASH SHA256=1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            PREFIX ${ABSEIL_BUILD_DIR}
            STAMP_DIR ${ABSEIL_BUILD_DIR}/stamps
            UPDATE_DISCONNECTED 1
            BUILD_ALWAYS 0
            CMAKE_ARGS
                -DCMAKE_C_COMPILER=${MUSL_GCC}
                -DCMAKE_CXX_COMPILER=clang++
                -DCMAKE_CXX_STANDARD=17
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${ABSEIL_PREFIX}
                -DBUILD_SHARED_LIBS=OFF
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_C_FLAGS=-O3\ -fPIC
                -DCMAKE_CXX_FLAGS=-O3\ -fPIC\ -target\ x86_64-linux-musl\ -stdlib=libc++
            BUILD_BYPRODUCTS
                ${ABSEIL_PREFIX}/lib/libabsl_base.a
                ${ABSEIL_PREFIX}/lib/libabsl_strings.a
            LOG_DOWNLOAD TRUE
            LOG_CONFIGURE TRUE
            LOG_BUILD TRUE
            LOG_INSTALL TRUE
            LOG_OUTPUT_ON_FAILURE TRUE
        )
    else()
        message(STATUS "  ${BoldBlue}Abseil${ColorReset} library found in cache: ${BoldMagenta}${ABSEIL_PREFIX}/lib/libabsl_base.a${ColorReset}")
        # Create a dummy target so dependencies can reference it
        add_custom_target(abseil-musl)
    endif()

    set(ABSEIL_LIBRARIES "${ABSEIL_PREFIX}/lib" PARENT_SCOPE)
    set(ABSEIL_INCLUDE_DIRS "${ABSEIL_PREFIX}/include" PARENT_SCOPE)

    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ABSEIL absl QUIET)

    if(ABSEIL_FOUND)
        set(ABSEIL_LIBRARIES ${ABSEIL_LDFLAGS} PARENT_SCOPE)
        set(ABSEIL_INCLUDE_DIRS ${ABSEIL_INCLUDE_DIRS} PARENT_SCOPE)
        message(STATUS "${BoldGreen}✓${ColorReset} Abseil: ${ABSEIL_LDFLAGS}")
    else()
        message(STATUS "${BoldYellow}⚠${ColorReset} Abseil not found in system packages, will try to find CMake package")
        find_package(absl QUIET)
        if(absl_FOUND)
            set(ABSEIL_LIBRARIES absl::base absl::strings PARENT_SCOPE)
            set(ABSEIL_INCLUDE_DIRS "" PARENT_SCOPE)
            message(STATUS "${BoldGreen}✓${ColorReset} Abseil found via CMake")
        else()
            message(WARNING "Abseil not found - some WebRTC features may be unavailable")
            set(ABSEIL_LIBRARIES "" PARENT_SCOPE)
            set(ABSEIL_INCLUDE_DIRS "" PARENT_SCOPE)
        endif()
    endif()

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ABSEIL absl QUIET)

    if(ABSEIL_FOUND)
        set(ABSEIL_LIBRARIES ${ABSEIL_LDFLAGS} PARENT_SCOPE)
        set(ABSEIL_INCLUDE_DIRS ${ABSEIL_INCLUDE_DIRS} PARENT_SCOPE)
        message(STATUS "${BoldGreen}✓${ColorReset} Abseil: ${ABSEIL_LDFLAGS}")
    else()
        message(STATUS "${BoldYellow}⚠${ColorReset} Abseil not found in system packages")
        set(ABSEIL_LIBRARIES "" PARENT_SCOPE)
        set(ABSEIL_INCLUDE_DIRS "" PARENT_SCOPE)
    endif()

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(absl CONFIG QUIET)
    if(absl_FOUND)
        set(ABSEIL_LIBRARIES absl::base absl::strings PARENT_SCOPE)
        set(ABSEIL_INCLUDE_DIRS "" PARENT_SCOPE)
        message(STATUS "${BoldGreen}✓${ColorReset} Abseil (Windows vcpkg)")
    else()
        message(WARNING "Abseil not found via vcpkg")
        set(ABSEIL_LIBRARIES "" PARENT_SCOPE)
        set(ABSEIL_INCLUDE_DIRS "" PARENT_SCOPE)
    endif()

else()
    message(FATAL_ERROR "Unsupported platform for Abseil")
endif()
