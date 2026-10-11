# Cross builds must not accidentally link the host's FFTW installation.
if(USE_MUSL OR IOS)
    include(FetchContent)
    function(asciichat_build_fftw)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        set(BUILD_SHARED_LIBS OFF)
        set(BUILD_TESTS OFF)
        set(ENABLE_THREADS OFF)
        set(ENABLE_OPENMP OFF)
        set(ENABLE_FLOAT OFF)
        set(ENABLE_LONG_DOUBLE OFF)
        set(DISABLE_FORTRAN ON)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        FetchContent_Declare(asciichat_fftw
            URL https://www.fftw.org/fftw-3.3.10.tar.gz
            URL_HASH SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
        )
        FetchContent_MakeAvailable(asciichat_fftw)
        set(FFTW_INCLUDE_DIRS "${asciichat_fftw_SOURCE_DIR}/api" PARENT_SCOPE)
    endfunction()
    asciichat_build_fftw()
    set(FFTW_LIBRARIES fftw3)
    set(FFTW_SYS_LIBRARIES fftw3)
    set(FFTW_SYS_INCLUDE_DIRS ${FFTW_INCLUDE_DIRS})
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
