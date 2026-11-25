# Cross builds must not accidentally link the host's FFTW installation.
if(USE_MUSL OR IOS OR EMSCRIPTEN)
    include(FetchContent)
    function(asciichat_build_fftw)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        set(BUILD_TESTS OFF)
        set(ENABLE_THREADS OFF)
        set(ENABLE_OPENMP OFF)
        set(ENABLE_FLOAT ON CACHE BOOL "Build the FFTW single-precision library" FORCE)
        set(ENABLE_LONG_DOUBLE OFF)
        set(DISABLE_FORTRAN ON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        asciichat_fetchcontent_declare(asciichat_fftw
            SOURCE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/fftw-src"
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/fftw-build"
            EXCLUDE_FROM_ALL
            URL https://www.fftw.org/fftw-3.3.10.tar.gz
            URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
        )
        FetchContent_MakeAvailable(asciichat_fftw)
        if(NOT TARGET asciichat-fftw)
            add_library(asciichat-fftw INTERFACE)
            target_link_libraries(asciichat-fftw INTERFACE fftw3f)
            target_include_directories(asciichat-fftw INTERFACE
                "${asciichat_fftw_SOURCE_DIR}/api")
        endif()
        if(NOT TARGET asciichat-fftw-float)
            add_library(asciichat-fftw-float INTERFACE)
            target_link_libraries(asciichat-fftw-float INTERFACE fftw3f)
            target_include_directories(asciichat-fftw-float INTERFACE
                "${asciichat_fftw_SOURCE_DIR}/api")
        endif()
    endfunction()
    asciichat_build_fftw()
    return()
endif()

if(WIN32)
    # The vcpkg FFTW port exposes archives but no FFTW3Config.cmake file.
    find_path(_asciichat_fftw_include NAMES fftw3.h PATHS "${VCPKG_INCLUDE_PATH}" REQUIRED)
    find_library(_fftw3_library NAMES fftw3 PATHS "${VCPKG_LIB_PATH}" REQUIRED)
    find_library(_fftw3f_library NAMES fftw3f PATHS "${VCPKG_LIB_PATH}" REQUIRED)
    add_library(asciichat-fftw-system UNKNOWN IMPORTED GLOBAL)
    set_target_properties(asciichat-fftw-system PROPERTIES
        IMPORTED_LOCATION "${_fftw3_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_asciichat_fftw_include}")
    add_library(asciichat-fftw-float-system UNKNOWN IMPORTED GLOBAL)
    set_target_properties(asciichat-fftw-float-system PROPERTIES
        IMPORTED_LOCATION "${_fftw3f_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_asciichat_fftw_include}")
    add_library(asciichat-fftw INTERFACE)
    target_link_libraries(asciichat-fftw INTERFACE asciichat-fftw-system)
    target_include_directories(asciichat-fftw INTERFACE "${_asciichat_fftw_include}")
    add_library(asciichat-fftw-float INTERFACE)
    target_link_libraries(asciichat-fftw-float INTERFACE asciichat-fftw-float-system)
    target_include_directories(asciichat-fftw-float INTERFACE "${_asciichat_fftw_include}")
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../utils/FindDependency.cmake)
find_dependency_library(
    NAME FFTW
    VCPKG_NAMES fftw3 libfftw3-3
    HEADER fftw3.h
    PKG_CONFIG fftw3
    HOMEBREW_PKG fftw
    STATIC_LIB_NAME libfftw3.a
    REQUIRED
)

# Some distributions package the double-precision FFTW library but omit the
# single-precision variant used by ascii-chat's audio and video transforms.
# Build that missing variant locally instead of making otherwise valid system
# dependency configurations fail at a later target-link step.
find_library(_asciichat_fftw_float_library NAMES fftw3f)
if(NOT _asciichat_fftw_float_library)
    include(FetchContent)
    function(asciichat_build_fftw_float)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        set(BUILD_TESTS OFF)
        set(ENABLE_THREADS OFF)
        set(ENABLE_OPENMP OFF)
        set(ENABLE_FLOAT ON CACHE BOOL "Build the FFTW single-precision library" FORCE)
        set(ENABLE_LONG_DOUBLE OFF)
        set(DISABLE_FORTRAN ON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        asciichat_fetchcontent_declare(asciichat_fftw_float
            SOURCE_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/fftw-float-src"
            BINARY_DIR "${ASCIICHAT_DEPS_CACHE_DIR}/build/fftw-float-build"
            EXCLUDE_FROM_ALL
            URL https://www.fftw.org/fftw-3.3.10.tar.gz
            URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
        )
        FetchContent_MakeAvailable(asciichat_fftw_float)
        set(_asciichat_fftw_float_library fftw3f PARENT_SCOPE)
        set(_asciichat_fftw_include "${asciichat_fftw_float_SOURCE_DIR}/api" PARENT_SCOPE)
    endfunction()
    asciichat_build_fftw_float()
endif()

# The pkg-config result can include the float library as a bare `fftw3f` item.
# Link the resolved archive or target below instead; passing the bare name to
# Clang is interpreted as an input filename and breaks shared-library links.
get_target_property(_asciichat_fftw_links asciichat-fftw INTERFACE_LINK_LIBRARIES)
list(REMOVE_ITEM _asciichat_fftw_links fftw3f)
set_property(TARGET asciichat-fftw PROPERTY INTERFACE_LINK_LIBRARIES
    "${_asciichat_fftw_links}")

if(NOT TARGET asciichat-fftw)
    add_library(asciichat-fftw INTERFACE)
    if(_asciichat_fftw_include)
        target_include_directories(asciichat-fftw INTERFACE "${_asciichat_fftw_include}")
    endif()
endif()
if(NOT TARGET asciichat-fftw-float)
    add_library(asciichat-fftw-float INTERFACE)
    if(TARGET FFTW3::fftw3f)
        target_link_libraries(asciichat-fftw-float INTERFACE FFTW3::fftw3f)
    elseif(_asciichat_fftw_float_library)
        target_link_libraries(asciichat-fftw-float INTERFACE "${_asciichat_fftw_float_library}")
    else()
        message(FATAL_ERROR "FFTW float target is required")
    endif()
    if(_asciichat_fftw_include)
        target_include_directories(asciichat-fftw-float INTERFACE "${_asciichat_fftw_include}")
    endif()
endif()
