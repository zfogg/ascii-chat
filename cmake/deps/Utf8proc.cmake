include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_utf8proc)
    # gotel vendors the same utf8proc target on Windows. Let that dependency
    # own the target there so CMake does not create two targets named utf8proc;
    # the public target and variable remain identical for ascii-chat callers.
    if(WIN32)
        set(UTF8PROC_LIBRARIES utf8proc PARENT_SCOPE)
        return()
    endif()
    asciichat_dependency_defaults()
    set(UTF8PROC_ENABLE_TESTING OFF)
    set(UTF8PROC_INSTALL ON)
    get_filename_component(_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../deps/ascii-chat-deps/utf8proc" ABSOLUTE)
    # Keep upstream's install rules for the library, headers and package metadata.
    add_subdirectory("${_source}" "${CMAKE_BINARY_DIR}/_deps/utf8proc")
endfunction()
asciichat_build_utf8proc()
set(UTF8PROC_LIBRARIES utf8proc)
