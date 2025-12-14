# =============================================================================
# OS-Specific Module
# =============================================================================
# Platform-specific implementations (webcam capture)
# This module's sources are added to ascii-chat-platform
# =============================================================================

# Webcam abstraction layer (common interface)
set(OS_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/webcam.c
)

# Platform-specific webcam implementations
if(WIN32)
    list(APPEND OS_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/windows/webcam_mediafoundation.c
    )
elseif(PLATFORM_DARWIN)
    list(APPEND OS_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/macos/webcam_avfoundation.m
    )
elseif(PLATFORM_LINUX)
    list(APPEND OS_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/linux/webcam_v4l2.c
    )
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}. No webcam implementation available.")
endif()

# Export to parent scope for inclusion in platform module
set(OS_WEBCAM_SRCS ${OS_SRCS} PARENT_SCOPE)

# Note: These sources are added to ascii-chat-video module via the parent CMakeLists.txt
# They're not a separate target since they depend on video frame types
