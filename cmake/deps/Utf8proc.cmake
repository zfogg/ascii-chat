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
    # lib/CMakeLists.txt owns the single utf8proc target used by ascii-chat.
    # Keeping a second generic `utf8proc` target here conflicts with gotel's
    # vendored target when both dependency graphs are configured.
endfunction()
asciichat_build_utf8proc()
set(UTF8PROC_LIBRARIES utf8proc)
