# =============================================================================
# Image to ASCII Module (ascii-chat-video)
# =============================================================================
# Video/image processing and ASCII art conversion
# =============================================================================

# Include SIMD subdirectory first (creates ascii-chat-simd)
add_subdirectory(simd)

# =============================================================================
# Video Processing Sources
# =============================================================================
set(VIDEO_SRCS
    ${CMAKE_SOURCE_DIR}/lib/video_frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ascii.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ansi_fast.c
)

create_ascii_chat_module(ascii-chat-video "${VIDEO_SRCS}")

# =============================================================================
# Dependencies
# =============================================================================

if(NOT BUILDING_OBJECT_LIBS)
    target_link_libraries(ascii-chat-video
        ascii-chat-util
        ascii-chat-platform
        ascii-chat-core
        ascii-chat-simd
    )
endif()

# Export sources to parent scope for unified library builds
set(VIDEO_SRCS ${VIDEO_SRCS} PARENT_SCOPE)
set(SIMD_SRCS ${SIMD_SRCS} PARENT_SCOPE)
