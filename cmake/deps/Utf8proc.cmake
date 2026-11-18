include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_utf8proc)
    # gotel vendors the same utf8proc target on Windows. Let that dependency
    # own the target there so CMake does not create two targets named utf8proc.
    if(WIN32)
        return()
    endif()
    asciichat_dependency_defaults()
    # libplatform owns this target in the full application graph.  Standalone
    # dependency consumers still need the module to provide the same contract.
    if(NOT TARGET asciichat-utf8proc)
        get_filename_component(_asciichat_repo_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
        add_library(asciichat-utf8proc STATIC
            "${_asciichat_repo_root}/deps/ascii-chat-deps/utf8proc/utf8proc.c")
        target_include_directories(asciichat-utf8proc PUBLIC
            "${_asciichat_repo_root}/deps/ascii-chat-deps/utf8proc")
        target_compile_definitions(asciichat-utf8proc PUBLIC UTF8PROC_STATIC=1)
    endif()
endfunction()
asciichat_build_utf8proc()
