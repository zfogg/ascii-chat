include_guard(GLOBAL)

# Source locations belong to the build checkout, independently of the runtime cwd.
function(asciichat_configure_source_root source_root binary_root)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ASCIICHAT_ENABLE_COVERAGE)
        # Release compiler flags already make project source locations relative.
        set(ASCIICHAT_SOURCE_ROOT "")
    else()
        string(REPLACE "\\" "/" ASCIICHAT_SOURCE_ROOT "${source_root}")
    endif()
    string(REPLACE "\"" "\\\"" ASCIICHAT_SOURCE_ROOT "${ASCIICHAT_SOURCE_ROOT}")
    configure_file(
        "${source_root}/include/ascii-chat/source_root.h.in"
        "${binary_root}/generated/ascii-chat/source_root.h"
        @ONLY
    )
endfunction()
