# Compatibility finder for vendored projects that call find_package(ZLIB).
# The application itself consumes asciichat-zlib; these values are only the
# standard FindZLIB result expected by third-party CMake code.
if(TARGET asciichat-zlib)
    set(ZLIB_FOUND TRUE)
    get_target_property(_asciichat_zlib_library asciichat-zlib
        ASCIICHAT_PROVIDER_LIBRARY)
    if(_asciichat_zlib_library AND
       NOT _asciichat_zlib_library STREQUAL "_asciichat_zlib_library-NOTFOUND")
        set(ZLIB_LIBRARY "${_asciichat_zlib_library}")
        set(ZLIB_LIBRARIES "${_asciichat_zlib_library}")
    else()
        set(ZLIB_LIBRARY asciichat-zlib)
        set(ZLIB_LIBRARIES asciichat-zlib)
    endif()
    get_target_property(_asciichat_zlib_includes asciichat-zlib
        INTERFACE_INCLUDE_DIRECTORIES)
    if(_asciichat_zlib_includes STREQUAL "_asciichat_zlib_includes-NOTFOUND")
        set(_asciichat_zlib_includes "")
    endif()
    set(ZLIB_INCLUDE_DIRS "${_asciichat_zlib_includes}")
    set(ZLIB_INCLUDE_DIR "${_asciichat_zlib_includes}")
    set(ZLIB_VERSION_STRING 1.3.1)
    set(ZLIB_VERSION 1.3.1)
else()
    include("${CMAKE_ROOT}/Modules/FindZLIB.cmake")
endif()
