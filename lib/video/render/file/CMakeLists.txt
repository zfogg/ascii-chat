# =============================================================================
# Render-to-File Module: libvterm + FreeType2 + fontconfig
# =============================================================================
# Cross-platform pixel renderer for ASCII art video output
# Uses libvterm for terminal emulation and FreeType2 for rasterization
# =============================================================================

add_library(ascii-chat-render-file OBJECT)

target_sources(ascii-chat-render-file PRIVATE
    terminal.c
)

target_include_directories(ascii-chat-render-file
    PRIVATE ${RENDER_FILE_INCLUDES}
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Dependencies are provided via RENDER_FILE_LIBS and RENDER_FILE_INCLUDES
# which are set by cmake/dependencies/Ghostty.cmake
