# =============================================================================
# Module 1: Utilities (ultra-stable - changes rarely)
# =============================================================================
# Utility source files providing general-purpose functionality

set(UTIL_SRCS
    lib/util/format.c
    lib/util/parsing.c
    lib/util/path.c
    lib/util/string.c
    lib/util/ip.c
    lib/util/aspect_ratio.c
    lib/util/time.c
    lib/util/image.c
    lib/util/audio.c
    lib/util/password.c
    lib/util/fps.c
)

# Add C23 compatibility wrappers for musl (provides __isoc23_* symbols)
if(USE_MUSL)
    list(APPEND UTIL_SRCS lib/platform/compat/musl_c23_compat.c)
endif()
