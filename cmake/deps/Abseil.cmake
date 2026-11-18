# =============================================================================
# Abseil-cpp (Google C++ Library) Dependency
# =============================================================================
# Configuration for Abseil-cpp, Google's C++ library used by WebRTC dependencies
#
# For musl builds: Built from source
# For native builds: Uses system package manager or builds from source
#
# Contract:
#   - asciichat-abseil: canonical target carrying Abseil usage requirements
# =============================================================================


include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(PLATFORM_IOS OR USE_MUSL OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE
   OR (APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT BUILD_SHARED_LIBS))
    function(asciichat_build_abseil)
        asciichat_dependency_defaults()
        set(ABSL_BUILD_TESTING OFF)
        set(ABSL_PROPAGATE_CXX_STD ON)
        if(WIN32)
            # Upstream's DLL exports also apply to GNU-style Clang with the MSVC ABI.
            set(ABSL_BUILD_MONOLITHIC_SHARED_LIBS ${BUILD_SHARED_LIBS})
        endif()
        asciichat_fetchcontent_declare(asciichat_abseil
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/abseil"
            EXCLUDE_FROM_ALL
            URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20250814.1.tar.gz
            URL_HASH SHA256=1692f77d1739bacf3f94337188b78583cf09bab7e420d2dc6c5605a4f86785a1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(asciichat_abseil)
    endfunction()
    asciichat_build_abseil()
    if(NOT TARGET asciichat-abseil)
        add_library(asciichat-abseil INTERFACE)
        target_link_libraries(asciichat-abseil INTERFACE absl::base absl::strings)
        target_include_directories(asciichat-abseil INTERFACE
            "$<BUILD_INTERFACE:${asciichat_abseil_SOURCE_DIR}>")
    endif()
    return()
endif()

# Non-musl builds: Use system package manager
if(UNIX AND NOT APPLE)
    # Linux/BSD: Use system package managers
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_asciichat_abseil IMPORTED_TARGET absl QUIET)

    if(_asciichat_abseil_FOUND AND TARGET PkgConfig::_asciichat_abseil)
        add_library(asciichat-abseil INTERFACE)
        target_link_libraries(asciichat-abseil INTERFACE PkgConfig::_asciichat_abseil)
        message(STATUS "${BoldGreen}✓${ColorReset} Abseil: ${_asciichat_abseil_LDFLAGS}")
    else()
        message(STATUS "${BoldYellow}⚠${ColorReset} Abseil not found in system packages, will try to find CMake package")
        find_package(absl CONFIG QUIET
            PATHS
                "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake"
                "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/absl"
                "/usr/lib/x86_64-linux-gnu/cmake/absl"
                "/usr/lib/cmake"
                "/usr/local/lib/cmake"
            NO_DEFAULT_PATH)
        if(NOT TARGET absl::base)
            unset(_asciichat_absl_config CACHE)
            if(EXISTS "/usr/lib/x86_64-linux-gnu/cmake/absl/abslConfig.cmake")
                include("/usr/lib/x86_64-linux-gnu/cmake/absl/abslConfig.cmake")
            endif()
        endif()
        if(NOT TARGET absl::base)
            find_file(_asciichat_absl_config
                NAMES abslConfig.cmake
                PATHS
                    "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/absl"
                    "/usr/lib/x86_64-linux-gnu/cmake/absl"
                    "/usr/lib/aarch64-linux-gnu/cmake/absl"
                NO_DEFAULT_PATH)
            if(_asciichat_absl_config)
                include("${_asciichat_absl_config}")
            endif()
        endif()
        if(TARGET absl::base)
            add_library(asciichat-abseil INTERFACE)
            target_link_libraries(asciichat-abseil INTERFACE absl::base absl::strings)
            message(STATUS "${BoldGreen}✓${ColorReset} Abseil found via CMake")
        else()
            message(FATAL_ERROR "Abseil is required; install the Abseil development package or enable the bundled dependency build")
        endif()
    endif()

elseif(APPLE)
    # macOS: Use homebrew or macports
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(_asciichat_abseil IMPORTED_TARGET absl QUIET)

    if(_asciichat_abseil_FOUND AND TARGET PkgConfig::_asciichat_abseil)
        add_library(asciichat-abseil INTERFACE)
        target_link_libraries(asciichat-abseil INTERFACE PkgConfig::_asciichat_abseil)
        message(STATUS "${BoldGreen}✓${ColorReset} Abseil: ${_asciichat_abseil_LDFLAGS}")
    else()
        message(STATUS "${BoldYellow}⚠${ColorReset} Abseil not found in system packages")
        find_package(absl CONFIG REQUIRED)
        add_library(asciichat-abseil INTERFACE)
        target_link_libraries(asciichat-abseil INTERFACE absl::base absl::strings)
    endif()

elseif(WIN32)
    # Windows: Use vcpkg
    find_package(absl CONFIG QUIET
        PATHS "${VCPKG_LIB_PATH}/.."
        NO_DEFAULT_PATH)
    if(TARGET absl::base)
        add_library(asciichat-abseil INTERFACE)
        target_link_libraries(asciichat-abseil INTERFACE absl::base absl::strings)
        message(STATUS "${BoldGreen}✓${ColorReset} Abseil (Windows vcpkg)")

        # Fixup: vcpkg abseil injects raw MSVC linker flags (-ignore:4221) in
        # INTERFACE_LINK_LIBRARIES. Clang driver doesn't recognize these.
        # Strip them from all imported absl:: targets.
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            # Get all target names from abseil's cmake config
            file(STRINGS "${absl_DIR}/abslTargets.cmake" _absl_tgt_lines REGEX "^set_target_properties\\(absl::")
            set(_absl_fixed_count 0)
            foreach(_line IN LISTS _absl_tgt_lines)
                string(REGEX MATCH "absl::[a-zA-Z0-9_]+" _tgt "${_line}")
                if(_tgt AND TARGET "${_tgt}")
                    get_target_property(_libs "${_tgt}" INTERFACE_LINK_LIBRARIES)
                    if(_libs)
                        string(FIND "${_libs}" "-ignore:" _has_ignore)
                        if(NOT _has_ignore EQUAL -1)
                            # Remove -ignore:NNNN and $<LINK_ONLY:-ignore:NNNN> entries
                            list(FILTER _libs EXCLUDE REGEX "-ignore:[0-9]+")
                            set_target_properties("${_tgt}" PROPERTIES INTERFACE_LINK_LIBRARIES "${_libs}")
                            math(EXPR _absl_fixed_count "${_absl_fixed_count} + 1")
                        endif()
                    endif()
                endif()
            endforeach()
            if(_absl_fixed_count GREATER 0)
                message(STATUS "  Stripped MSVC -ignore:4221 flags from ${_absl_fixed_count} abseil targets for Clang")
            endif()
        endif()
    else()
        message(FATAL_ERROR "Abseil is required; install the vcpkg Abseil package")
    endif()

else()
    message(FATAL_ERROR "Unsupported platform for Abseil")
endif()

if(NOT TARGET asciichat-abseil)
    message(FATAL_ERROR "Abseil is required for the native WebRTC audio dependency")
endif()
