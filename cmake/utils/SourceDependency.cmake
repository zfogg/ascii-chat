include_guard(GLOBAL)
include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/../toolchains/MuslFlags.cmake")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../find")

# Declare a source dependency in the shared cache.  Dependency modules may
# still provide an explicit BINARY_DIR for a special multi-stage build, but
# ordinary FetchContent projects all get a stable per-dependency build tree
# beneath the selected configuration cache.
function(asciichat_fetchcontent_declare name)
    set(_arguments ${ARGN})
    list(FIND _arguments BINARY_DIR _binary_dir_index)
    if(_binary_dir_index EQUAL -1)
        list(APPEND _arguments BINARY_DIR
            "${ASCIICHAT_DEPS_CACHE_DIR}/build/${name}")
    endif()
    FetchContent_Declare(${name} ${_arguments})
endfunction()

# Call inside a function so upstream options do not change sibling projects.
macro(asciichat_dependency_defaults)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS ON)
    set(CMAKE_CXX_EXTENSIONS ON)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    set(BUILD_TESTING OFF)
    set(BUILD_TESTS OFF)
    if(USE_MUSL)
        set(CMAKE_C_COMPILER_TARGET "${MUSL_TARGET}")
        set(CMAKE_CXX_COMPILER_TARGET "${MUSL_TARGET}")
        asciichat_musl_compile_flags(_dependency_c_flags _dependency_cxx_flags)
        string(APPEND CMAKE_C_FLAGS " ${_dependency_c_flags}")
        string(APPEND CMAKE_CXX_FLAGS " ${_dependency_cxx_flags}")
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    endif()
endmacro()
