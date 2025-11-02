# =============================================================================
# Post-Build Processing for Release Builds
# =============================================================================
# Handles post-build optimization steps for Release builds:
#   - Symbol stripping (removes debug info and reduces binary size)
#   - Path removal (removes embedded source paths for privacy)
#   - .comment section cleanup (Linux ELF only)
#
# Prerequisites (must be set before including this file):
#   - CMAKE_BUILD_TYPE: Build type
#   - TARGET: Name of the target to process (must exist before calling)
#   - CMAKE_SOURCE_DIR, CMAKE_BINARY_DIR: Project paths
#   - WIN32, UNIX, APPLE: Platform detection
#
# Tools required:
#   - Linux: strip, objcopy, bash (for path removal)
#   - Windows: strip, PowerShell (for path removal)
#   - macOS: strip
# =============================================================================

# Strip symbols in Release builds and clean .comment section
# Note: .comment section is ELF-specific (Linux only)
# Windows uses PE format (no .comment section)
# macOS uses Mach-O format (no .comment section)
# Also ensure no debug info paths are embedded
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    find_program(STRIP_EXECUTABLE strip)
    find_program(OBJCOPY_EXECUTABLE objcopy)
    if(STRIP_EXECUTABLE AND OBJCOPY_EXECUTABLE)
        # Only run .comment cleaning on Linux (ELF format)
        # Windows (PE) and macOS (Mach-O) don't have .comment sections
        if(UNIX AND NOT APPLE AND NOT WIN32)
            add_custom_command(TARGET ascii-chat POST_BUILD
                # Clean .comment section BEFORE stripping (remove duplicates, keep version string)
                COMMAND ${CMAKE_SOURCE_DIR}/cmake/clean_comment.sh $<TARGET_FILE:ascii-chat>
                # Strip symbols but keep custom sections
                # Use --strip-debug instead of --strip-unneeded to preserve custom sections
                COMMAND ${STRIP_EXECUTABLE} --strip-debug --keep-section=.comment --keep-section=.ascii_chat --keep-section=.version $<TARGET_FILE:ascii-chat>
                COMMENT "Deduplicating .comment section and stripping debug symbols"
            )
            # Remove embedded file paths from binary using bash script (much faster than PowerShell)
            # Note: DEPENDS is not supported for TARGET form, but the script path is explicit in COMMAND
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND bash "${CMAKE_SOURCE_DIR}/cmake/remove_paths.sh"
                    "$<TARGET_FILE:ascii-chat>"
                    "${CMAKE_SOURCE_DIR}"
                    "${CMAKE_BINARY_DIR}"
                COMMENT "Removing embedded file paths from ascii-chat binary"
            )
        else()
            # On Windows, use strip to remove debug info and paths
            # Note: Windows PE format doesn't have .comment sections
            if(WIN32)
                # Strip symbols first
                add_custom_command(TARGET ascii-chat POST_BUILD
                    COMMAND ${STRIP_EXECUTABLE} --strip-all $<TARGET_FILE:ascii-chat>
                    COMMENT "Stripping symbols and debug info from ascii-chat"
                )
                # Then remove embedded file paths from binary
                find_program(POWERSHELL_EXECUTABLE pwsh powershell)
                if(POWERSHELL_EXECUTABLE)
                    # Note: DEPENDS is not supported for TARGET form, but the script path is explicit in COMMAND
                    add_custom_command(TARGET ascii-chat POST_BUILD
                        COMMAND ${POWERSHELL_EXECUTABLE} -ExecutionPolicy Bypass -File
                            "${CMAKE_SOURCE_DIR}/cmake/remove_paths.ps1"
                            "$<TARGET_FILE:ascii-chat>"
                            "${CMAKE_SOURCE_DIR}"
                            "${CMAKE_BINARY_DIR}"
                        COMMENT "Removing embedded file paths from ascii-chat"
                    )
                else()
                    message(WARNING "PowerShell not found - cannot remove embedded paths from binary")
                endif()
            else()
                # macOS: strip symbols
                add_custom_command(TARGET ascii-chat POST_BUILD
                    COMMAND ${STRIP_EXECUTABLE} --strip-all $<TARGET_FILE:ascii-chat>
                    COMMENT "Stripping symbols from ascii-chat"
                )
            endif()
        endif()
    elseif(STRIP_EXECUTABLE)
        # Fallback: just strip symbols if objcopy not available
        add_custom_command(TARGET ascii-chat POST_BUILD
            COMMAND ${STRIP_EXECUTABLE} --strip-all $<TARGET_FILE:ascii-chat>
            COMMENT "Stripping symbols from ascii-chat"
        )
    endif()
endif()
