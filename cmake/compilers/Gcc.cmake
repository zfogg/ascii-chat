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
    # Use the checked-in defer Python/libclang backend for GCC. The published
    # native defer archives predate GCC support and probe GCC with Clang's
    # -print-resource-dir option. GCC remains the native application compiler
    # and uses its own binutils below; only source transformation is delegated
    # to libclang, as it is for MSVC.
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(_gcc_llvm_config NAMES llvm-config llvm-config-20 llvm-config-19 llvm-config-18 REQUIRED)
    execute_process(
        COMMAND "${_gcc_llvm_config}" --libdir
        OUTPUT_VARIABLE _gcc_llvm_libdir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _gcc_llvm_config_result
    )
    if(NOT _gcc_llvm_config_result EQUAL 0 OR NOT _gcc_llvm_libdir)
        message(FATAL_ERROR "llvm-config could not report the libclang directory for GCC defer transformations")
    endif()
    find_file(_gcc_libclang NAMES libclang.so libclang.so.1
        PATHS "${_gcc_llvm_libdir}" NO_DEFAULT_PATH REQUIRED)
    set(RECAST_BACKEND PYTHON CACHE STRING "Recast transformer backend" FORCE)
    set(RECAST_TRANSFORMS defer CACHE STRING "Recast transforms" FORCE)
    set(RECAST_TOOL_ROOT "${CMAKE_SOURCE_DIR}/deps/recast" CACHE PATH "Recast source tree" FORCE)
    set(RECAST_PYTHON_COMMAND "${Python3_EXECUTABLE}" CACHE FILEPATH "Recast Python interpreter" FORCE)
    set(RECAST_LIBCLANG_PATH "${_gcc_libclang}" CACHE FILEPATH "libclang shared library" FORCE)
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
