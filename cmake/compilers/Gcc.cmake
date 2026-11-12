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
    # GCC remains the project compiler. Use the source-backed Python/libclang
    # transformer for defer because the released native transformer currently
    # assumes every selected compiler supports Clang's resource-dir query.
    find_program(_gcc_python python3 python REQUIRED)
    find_file(_gcc_libclang NAMES libclang.so libclang.so.1
        PATHS /usr/lib /usr/lib64 /usr/lib/llvm/*/lib /usr/local/lib)
    if(NOT _gcc_libclang)
        message(FATAL_ERROR "GCC builds require libclang.so for defer source transformation")
    endif()
    set(DEFER_BACKEND PYTHON CACHE STRING "defer transformer backend" FORCE)
    set(DEFER_TOOL_ROOT "${CMAKE_SOURCE_DIR}/deps/defer" CACHE PATH "defer source tree" FORCE)
    set(DEFER_PYTHON_COMMAND "${_gcc_python}" CACHE FILEPATH "defer Python interpreter" FORCE)
    set(DEFER_LIBCLANG_PATH "${_gcc_libclang}" CACHE FILEPATH "libclang shared library" FORCE)
    message(STATUS "Using GCC toolchain: ${CMAKE_C_COMPILER}")
endfunction()

function(configure_gcc_post_project)
    # GCC flags are already the native dialect used by CompilerFlags.cmake.
endfunction()
