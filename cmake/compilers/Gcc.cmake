include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Common.cmake")

function(configure_gcc_pre_project)
    if(WIN32)
        message(FATAL_ERROR
            "GCC is not supported as a native Windows toolchain yet; use a Linux/WSL host for GCC. "
            "The Windows GCC dependency graph currently cannot bootstrap the repository's ICU host tools.")
    endif()
    find_program(_gcc gcc REQUIRED)
    find_program(_gxx g++ REQUIRED)
    find_program(_ar ar REQUIRED)
    find_program(_ranlib ranlib REQUIRED)
    find_program(_nm nm REQUIRED)
    find_program(_objdump objdump REQUIRED)
    foreach(_pair IN ITEMS "CMAKE_C_COMPILER;${_gcc}" "CMAKE_CXX_COMPILER;${_gxx}" "ASCIICHAT_GCC_AR_EXECUTABLE;${_ar}" "ASCIICHAT_GCC_RANLIB_EXECUTABLE;${_ranlib}" "ASCIICHAT_GCC_NM_EXECUTABLE;${_nm}" "ASCIICHAT_GCC_OBJDUMP_EXECUTABLE;${_objdump}")
        list(GET _pair 0 _name)
        list(GET _pair 1 _value)
        set(${_name} "${_value}" CACHE FILEPATH "GCC tool" FORCE)
    endforeach()
    # Run the native Linux defer transformer. This keeps source transformation
    # on the GCC host instead of sending a Python/libclang worker through a
    # Windows-mounted source tree.
    set(DEFER_BACKEND NATIVE CACHE STRING "defer transformer backend" FORCE)
    # GCC 11, still shipped by supported Ubuntu LTS releases, does not accept
    # C++26. Select the supported C++23 dialect before project() performs
    # compiler feature detection.
    set(CMAKE_C_STANDARD 23 CACHE STRING "C standard" FORCE)
    set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard" FORCE)
    set(GOTEL_CXX_STANDARD 23 CACHE STRING "C++ standard for gotel" FORCE)
    message(STATUS "Using GCC toolchain: ${CMAKE_C_COMPILER}")
endfunction()

function(configure_gcc_post_project)
    # GCC flags are already the native dialect used by CompilerFlags.cmake.
endfunction()
