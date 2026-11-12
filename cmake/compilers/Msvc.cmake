include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Common.cmake")

function(configure_msvc_pre_project)
    # Native cl.exe cannot parse the repository's defer marker syntax. Use the
    # standalone Python/libclang transformer for C sources, then compile the
    # generated sources with cl.exe as usual.
    find_program(_msvc_python python3 python)
    find_file(_msvc_libclang libclang.dll
        PATHS "$ENV{USERPROFILE}/scoop/apps/llvm/current/bin" "C:/Program Files/LLVM/bin")
    if(NOT _msvc_python OR NOT _msvc_libclang)
        message(FATAL_ERROR "MSVC builds require Python and LLVM libclang.dll for defer source transformation")
    endif()
    set(DEFER_BACKEND PYTHON CACHE STRING "defer transformer backend" FORCE)
    set(DEFER_TOOL_ROOT "${CMAKE_SOURCE_DIR}/deps/defer" CACHE PATH "defer source tree" FORCE)
    set(DEFER_PYTHON_COMMAND "${_msvc_python}" CACHE FILEPATH "defer Python interpreter" FORCE)
    set(DEFER_LIBCLANG_PATH "${_msvc_libclang}" CACHE FILEPATH "libclang shared library" FORCE)
    find_program(_vswhere vswhere PATHS "$ENV{ProgramFiles}/Microsoft Visual Studio/Installer" "C:/Program Files (x86)/Microsoft Visual Studio/Installer")
    if(_vswhere)
        execute_process(COMMAND "${_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath OUTPUT_VARIABLE _vs_root OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    file(GLOB _cl_candidates "C:/Program Files/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe" "C:/Program Files (x86)/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe")
    if(NOT _cl_candidates)
        message(FATAL_ERROR "MSVC was selected but cl.exe was not found. Install the Visual C++ workload.")
    endif()
    list(SORT _cl_candidates COMPARE NATURAL ORDER DESCENDING)
    list(GET _cl_candidates 0 _cl)
    get_filename_component(_msvc_bin "${_cl}" DIRECTORY)
    get_filename_component(_msvc_host "${_msvc_bin}" DIRECTORY)
    get_filename_component(_msvc_bin_root "${_msvc_host}" DIRECTORY)
    get_filename_component(_msvc_version "${_msvc_bin_root}" DIRECTORY)
    get_filename_component(_msvc_tools "${_msvc_version}" DIRECTORY)
    get_filename_component(_vc_root "${_msvc_tools}" DIRECTORY)
    get_filename_component(_vs_install "${_vc_root}" DIRECTORY)
    get_filename_component(_vs_install "${_vs_install}" DIRECTORY)
    find_file(_link link.exe PATHS "${_msvc_bin}" NO_DEFAULT_PATH REQUIRED)
    find_file(_lib lib.exe PATHS "${_msvc_bin}" NO_DEFAULT_PATH REQUIRED)
    find_file(_dumpbin dumpbin.exe PATHS "${_msvc_bin}" NO_DEFAULT_PATH REQUIRED)
    set(CMAKE_C_COMPILER "${_cl}" CACHE FILEPATH "MSVC C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_cl}" CACHE FILEPATH "MSVC C++ compiler" FORCE)
    # CMake's stock MSVC Debug flags add /RTC1, which conflicts with the
    # optimized SIMD translation units used by ascii-chat.
    set(CMAKE_C_FLAGS_DEBUG "/Od /Zi" CACHE STRING "MSVC Debug C flags" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi" CACHE STRING "MSVC Debug C++ flags" FORCE)
    set(GOTEL_CXX_STANDARD 23 CACHE STRING "C++ standard for gotel" FORCE)
    set(ASCIICHAT_MSVC_LINK_EXECUTABLE "${_link}" CACHE FILEPATH "MSVC linker" FORCE)
    set(ASCIICHAT_MSVC_LIB_EXECUTABLE "${_lib}" CACHE FILEPATH "MSVC librarian" FORCE)
    set(ASCIICHAT_MSVC_DUMPBIN_EXECUTABLE "${_dumpbin}" CACHE FILEPATH "MSVC dumpbin" FORCE)
    # CMake's Ninja MSVC rules need the SDK environment. vcvarsall supplies it.
    set(_vcvarsall "${_vs_install}/VC/Auxiliary/Build/vcvarsall.bat")
    if(NOT EXISTS "${_vcvarsall}")
        message(FATAL_ERROR "MSVC installation was found, but vcvarsall.bat is missing: ${_vcvarsall}")
    endif()
    file(GLOB _kits_lib "C:/Program Files (x86)/Windows Kits/10/Lib/*/um/x64")
    file(GLOB _kits_ucrt "C:/Program Files (x86)/Windows Kits/10/Lib/*/ucrt/x64")
    file(GLOB _kits_include "C:/Program Files (x86)/Windows Kits/10/Include/*")
    list(SORT _kits_lib COMPARE NATURAL ORDER DESCENDING)
    list(SORT _kits_ucrt COMPARE NATURAL ORDER DESCENDING)
    list(SORT _kits_include COMPARE NATURAL ORDER DESCENDING)
    list(GET _kits_lib 0 _kit_um)
    list(GET _kits_ucrt 0 _kit_ucrt)
    list(GET _kits_include 0 _kit_include)
    set(ENV{LIB} "${_msvc_version}/lib/x64;${_kit_ucrt};${_kit_um}")
    set(ENV{INCLUDE} "${_msvc_version}/include;${_vs_install}/VC/Auxiliary/VS/include;${_kit_include}/ucrt;${_kit_include}/um;${_kit_include}/shared;${_kit_include}/winrt;${_kit_include}/cppwinrt")
    set(ASCIICHAT_MSVC_INCLUDE_DIRS
        "${_msvc_version}/include"
        "${_vs_install}/VC/Auxiliary/VS/include"
        "${_kit_include}/ucrt"
        "${_kit_include}/um"
        "${_kit_include}/shared"
        "${_kit_include}/winrt"
        "${_kit_include}/cppwinrt"
        CACHE INTERNAL "MSVC and Windows SDK include directories")
    set(ASCIICHAT_MSVC_LIBRARY_DIRS
        "${_msvc_version}/lib/x64" "${_kit_ucrt}" "${_kit_um}"
        CACHE INTERNAL "MSVC and Windows SDK library directories")
    message(STATUS "Using MSVC toolchain: ${CMAKE_C_COMPILER}")
endfunction()

function(configure_msvc_post_project)
    # Persist the Visual Studio environment in generated build rules too. A
    # later `cmake --build` is a new process and cannot inherit the configure
    # process's vcvars environment.
    foreach(_include IN LISTS ASCIICHAT_MSVC_INCLUDE_DIRS)
        if(NOT CMAKE_C_FLAGS MATCHES "[ /]I\"${_include}\\\"")
            string(APPEND CMAKE_C_FLAGS " /I\"${_include}\"")
        endif()
        if(NOT CMAKE_CXX_FLAGS MATCHES "[ /]I\"${_include}\\\"")
            string(APPEND CMAKE_CXX_FLAGS " /I\"${_include}\"")
        endif()
    endforeach()
    foreach(_library IN LISTS ASCIICHAT_MSVC_LIBRARY_DIRS)
        if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "LIBPATH:\"${_library}\\\"")
            string(APPEND CMAKE_EXE_LINKER_FLAGS " /LIBPATH:\"${_library}\"")
        endif()
        if(NOT CMAKE_SHARED_LINKER_FLAGS MATCHES "LIBPATH:\"${_library}\\\"")
            string(APPEND CMAKE_SHARED_LINKER_FLAGS " /LIBPATH:\"${_library}\"")
        endif()
        if(NOT CMAKE_MODULE_LINKER_FLAGS MATCHES "LIBPATH:\"${_library}\\\"")
            string(APPEND CMAKE_MODULE_LINKER_FLAGS " /LIBPATH:\"${_library}\"")
        endif()
    endforeach()
    # MSVC's C11 atomics implementation is opt-in, but gotel and ascii-chat
    # use <stdatomic.h> directly.
    if(NOT CMAKE_C_FLAGS MATCHES "experimental:c11atomics")
        string(APPEND CMAKE_C_FLAGS " /experimental:c11atomics")
    endif()
    if(NOT CMAKE_CXX_FLAGS MATCHES "MsvcCompat.h")
        string(APPEND CMAKE_C_FLAGS " /FI\"${CMAKE_SOURCE_DIR}/cmake/compilers/MsvcCompat.h\"")
        string(APPEND CMAKE_CXX_FLAGS " /FI\"${CMAKE_SOURCE_DIR}/cmake/compilers/MsvcCompat.h\"")
    endif()
    if(NOT CMAKE_CXX_FLAGS MATCHES "Zc:__cplusplus")
        string(APPEND CMAKE_CXX_FLAGS " /Zc:__cplusplus")
    endif()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags" FORCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" CACHE STRING "CXX compiler flags" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "Executable linker flags" FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" CACHE STRING "Shared linker flags" FORCE)
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}" CACHE STRING "Module linker flags" FORCE)
endfunction()
