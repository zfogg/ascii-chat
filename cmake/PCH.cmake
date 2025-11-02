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
        # Add most commonly included headers as precompiled for the core module
        # Based on analysis: these headers appear in 30+ source files
        #
        # NOTE: We CANNOT use precompiled headers with common.h because it defines
        # malloc/free macros that conflict with system headers. When a PCH includes
        # common.h, every file using the PCH tries to redefine malloc/free when
        # system headers like <malloc.h> are included, causing compiler errors.
        #
        # Instead, we precompile only safe headers that don't have macro conflicts.
        target_precompile_headers(ascii-chat-core PRIVATE
            # Platform abstraction (safe - no conflicting macros)
            lib/platform/abstraction.h
            lib/platform/system.h
            lib/platform/init.h
            lib/platform/file.h
            lib/platform/thread.h
            lib/platform/string.h
            lib/platform/socket.h
            lib/platform/terminal.h
            
            # Core library headers (safe - no conflicting macros)
            lib/logging.h
            lib/options.h
            lib/buffer_pool.h
            lib/asciichat_errno.h
            lib/hashtable.h
            lib/ringbuffer.h
            lib/palette.h
            
            # Utility headers (frequently used)
            lib/util/path.h
            lib/util/format.h
            lib/util/math.h
            lib/util/parsing.h
            lib/util/aspect_ratio.h
            lib/util/ip.h
            
            # Crypto headers (commonly used)
            lib/crypto/known_hosts.h
            
            # Standard C headers (most frequently used, safe from macro conflicts)
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
        message(STATUS "${BoldGreen}Precompiled headers enabled (excluding common.h due to macro conflicts)${ColorReset}")
    elseif(USE_MUSL)
        message(STATUS "Precompiled headers disabled for musl builds")
    endif()
else()
    message(STATUS "Precompiled headers require CMake 3.16+ (you have ${CMAKE_VERSION})")
endif()

