include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_utf8proc)
    # gotel vendors the same utf8proc target on Windows. Let that dependency
    # own the target there so CMake does not create two targets named utf8proc.
    if(WIN32)
        return()
    endif()
    asciichat_dependency_defaults()
    # lib/CMakeLists.txt owns the single utf8proc target used by ascii-chat.
    # Keeping a second generic `utf8proc` target here conflicts with gotel's
    # vendored target when both dependency graphs are configured.
endfunction()
asciichat_build_utf8proc()
