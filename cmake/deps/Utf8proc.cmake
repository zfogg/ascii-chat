include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
function(asciichat_build_utf8proc)
    asciichat_dependency_defaults()
    # lib/CMakeLists.txt owns the single utf8proc target used by ascii-chat.
    # Keeping a second generic `utf8proc` target here conflicts with gotel's
    # vendored target when both dependency graphs are configured.
endfunction()
asciichat_build_utf8proc()
set(UTF8PROC_LIBRARIES ascii-chat-utf8proc)
