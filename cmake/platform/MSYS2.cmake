include_guard(GLOBAL)

set(ASCIICHAT_MSYS2_ROOT "$ENV{MSYS2_ROOT}" CACHE PATH "MSYS2 installation for Autotools dependencies")

if(NOT ASCIICHAT_MSYS2_ROOT)
    set(_msys2_roots
        "$ENV{SystemDrive}/msys64"
        "$ENV{USERPROFILE}/scoop/apps/msys2/current"
        "$ENV{SCOOP}/apps/msys2/current"
    )
    foreach(_vcpkg_root IN ITEMS "${VCPKG_ROOT}" "$ENV{VCPKG_ROOT}")
        if(_vcpkg_root)
            file(GLOB _vcpkg_msys2 LIST_DIRECTORIES TRUE "${_vcpkg_root}/downloads/tools/msys2/*")
            list(APPEND _msys2_roots ${_vcpkg_msys2})
        endif()
    endforeach()
    cmake_path(CONVERT "$ENV{PATH}" TO_CMAKE_PATH_LIST _msys2_path_entries NORMALIZE)
    foreach(_entry IN LISTS _msys2_path_entries)
        if(EXISTS "${_entry}/msys-2.0.dll")
            get_filename_component(_root "${_entry}/../.." ABSOLUTE)
            list(APPEND _msys2_roots "${_root}")
        endif()
    endforeach()
    find_program(_msys2_make NAMES make.exe
        HINTS ${_msys2_roots} PATH_SUFFIXES usr/bin NO_DEFAULT_PATH NO_CACHE)
    if(_msys2_make)
        get_filename_component(_bin "${_msys2_make}" DIRECTORY)
        get_filename_component(_root "${_bin}/../.." ABSOLUTE)
        set(ASCIICHAT_MSYS2_ROOT "${_root}" CACHE PATH "MSYS2 installation for Autotools dependencies" FORCE)
    endif()
endif()

set(MSYS2_BASH_EXECUTABLE "${ASCIICHAT_MSYS2_ROOT}/usr/bin/bash.exe")
set(MSYS2_MAKE_EXECUTABLE "${ASCIICHAT_MSYS2_ROOT}/usr/bin/make.exe")
if(NOT EXISTS "${MSYS2_BASH_EXECUTABLE}" OR NOT EXISTS "${MSYS2_MAKE_EXECUTABLE}")
    message(FATAL_ERROR
        "WASM libsodium requires MSYS2 Bash and Make from the same installation. "
        "Install MSYS2 and run 'pacman -S --needed base-devel', then set "
        "ASCIICHAT_MSYS2_ROOT or the MSYS2_ROOT environment variable if it is not detected.")
endif()
execute_process(COMMAND "${MSYS2_MAKE_EXECUTABLE}" --version
    OUTPUT_VARIABLE _msys2_make_version RESULT_VARIABLE _msys2_make_result)
if(NOT _msys2_make_result EQUAL 0 OR NOT _msys2_make_version MATCHES "-msys")
    message(FATAL_ERROR "${MSYS2_MAKE_EXECUTABLE} must be MSYS2 Make, not native Windows Make.")
endif()
message(STATUS "MSYS2 Autotools environment: ${ASCIICHAT_MSYS2_ROOT}")
