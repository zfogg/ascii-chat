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
    option(USE_PRECOMPILED_HEADERS "Use precompiled headers for faster builds" ON)

    if(USE_PRECOMPILED_HEADERS AND NOT USE_MUSL)
        set(_ascii_chat_pch_targets
            ascii-chat-core
            ascii-chat-network
            ascii-chat-platform
            ascii-chat-util
        )

        set(_ascii_chat_pch_headers
            lib/platform/abstraction.h
            lib/platform/system.h
            lib/platform/init.h
            lib/platform/file.h
            lib/platform/thread.h
            lib/platform/string.h
            lib/platform/socket.h
            lib/platform/terminal.h

            lib/logging.h
            lib/options.h
            lib/buffer_pool.h
            lib/asciichat_errno.h
            lib/ringbuffer.h
            lib/palette.h

            lib/util/path.h
            lib/util/format.h
            lib/util/math.h
            lib/util/parsing.h
            lib/util/aspect_ratio.h
            lib/util/ip.h

            lib/crypto/known_hosts.h

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
    elseif(USE_MUSL)
        message(STATUS "Precompiled headers disabled for ${BoldBlue}musl${ColorReset} builds")
    endif()
else()
    message(STATUS "Precompiled headers require ${BoldBlue}CMake${ColorReset} 3.16+ (you have ${CMAKE_VERSION})")
endif()

