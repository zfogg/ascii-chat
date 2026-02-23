# =============================================================================
# Precompiled Headers Configuration Module
# =============================================================================
# This module configures precompiled headers for faster compilation.
# Precompiled headers can speed up compilation by 20-30%.
#
# Prerequisites:
#   - Must run after ascii-chat-core target is created
#   - CMake 3.16+ required
#   - USE_MUSL must be checked (disabled for musl builds)
#
# Outputs:
#   - Precompiled headers enabled for ascii-chat-core target
# =============================================================================

# CMake 3.16+ supports target_precompile_headers
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    option(ASCIICHAT_USE_PCH "Use precompiled headers for faster builds" ON)

    if(ASCIICHAT_USE_PCH AND NOT USE_MUSL AND NOT ASCIICHAT_BUILD_WITH_PANIC)
        set(_ascii_chat_pch_targets
            ascii-chat-core
            ascii-chat-network
            ascii-chat-platform
            ascii-chat-util
        )

        set(_ascii_chat_pch_headers
            <ascii-chat/platform/api.h>
            <ascii-chat/platform/system.h>
            <ascii-chat/platform/init.h>
            <ascii-chat/platform/filesystem.h>
            <ascii-chat/platform/thread.h>
            <ascii-chat/platform/string.h>
            <ascii-chat/platform/socket.h>
            <ascii-chat/platform/terminal.h>

            <ascii-chat/log/logging.h>
            <ascii-chat/options/options.h>
            <ascii-chat/options/enums.h>
            <ascii-chat/options/layout.h>
            <ascii-chat/options/colorscheme.h>
            <ascii-chat/buffer_pool.h>
            <ascii-chat/asciichat_errno.h>
            <ascii-chat/ringbuffer.h>
            <ascii-chat/video/palette.h>

            <ascii-chat/util/path.h>
            <ascii-chat/util/format.h>
            <ascii-chat/util/math.h>
            <ascii-chat/util/parsing.h>
            <ascii-chat/util/aspect_ratio.h>
            <ascii-chat/util/ip.h>
            <ascii-chat/util/lifecycle.h>

            <ascii-chat/crypto/known_hosts.h>

            <stdio.h>
            <stdlib.h>
            <string.h>
            <stdbool.h>
            <stdint.h>
            <stddef.h>
            <stdatomic.h>
            <time.h>
            <errno.h>
            <limits.h>
            <stdarg.h>
            <inttypes.h>
            <ctype.h>
            <assert.h>
        )

        foreach(_pch_target IN LISTS _ascii_chat_pch_targets)
            if(TARGET ${_pch_target})
                get_target_property(_pch_existing ${_pch_target} PRECOMPILE_HEADERS)
                if(NOT _pch_existing)
                    target_precompile_headers(${_pch_target} PRIVATE ${_ascii_chat_pch_headers})
                endif()
            endif()
        endforeach()

        message(STATUS "${BoldGreen}Precompiled headers enabled for core libraries (excluding common.h due to macro conflicts)${ColorReset}")
    elseif(ASCIICHAT_BUILD_WITH_PANIC)
        message(STATUS "Precompiled headers disabled for ${BoldBlue}panic instrumentation${ColorReset} builds (include path conflicts)")
    elseif(USE_MUSL)
        message(STATUS "Precompiled headers disabled for ${BoldBlue}musl${ColorReset} builds")
    endif()
else()
    message(STATUS "Precompiled headers require ${BoldBlue}CMake${ColorReset} 3.16+ (you have ${CMAKE_VERSION})")
endif()

