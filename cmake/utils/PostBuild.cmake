# =============================================================================
# Post-Build Processing for Release Builds
# =============================================================================
# Handles post-build optimization steps for Release builds:
#   - Symbol stripping (removes debug info and reduces binary size)
#   - Path removal (removes embedded source paths for privacy)
#   - .comment section cleanup (Linux ELF only)
#   - DLL copying (Windows dynamic builds only)
#
# Prerequisites (must be set before including this file):
#   - CMAKE_BUILD_TYPE: Build type
#   - TARGET: Name of the target to process (must exist before calling)
#   - CMAKE_SOURCE_DIR, CMAKE_BINARY_DIR: Project paths
#   - WIN32, UNIX, APPLE: Platform detection
#
# Tools required:
#   - Linux: strip, objcopy, bash (for path removal)
#   - Windows: strip, bash (for path removal - Git Bash or WSL)
#   - macOS: strip
# =============================================================================

# Copy DLL dependencies and PDBs to bin directory on Windows (for non-static builds)
# Uses cmake -E copy for cross-platform path handling (generator expressions use forward slashes)
# vcpkg's applocal.ps1 is disabled via VCPKG_APPLOCAL_DEPS=OFF in Init.cmake
function(copy_windows_dlls TARGET_NAME)
    if(WIN32 AND NOT CMAKE_BUILD_TYPE MATCHES "Release")
        # Only copy DLLs for non-static builds (Debug, Dev builds use dynamic libraries)
        # Release builds use static libraries (x64-windows-static triplet) so no DLLs needed

        # Determine vcpkg installed directory - prefer _VCPKG_INSTALLED_DIR set by vcpkg toolchain
        # This is more reliable than $ENV{VCPKG_ROOT} which can change between configure and build
        if(DEFINED _VCPKG_INSTALLED_DIR)
            set(VCPKG_INSTALLED "${_VCPKG_INSTALLED_DIR}")
        elseif(DEFINED VCPKG_INSTALLED_DIR)
            set(VCPKG_INSTALLED "${VCPKG_INSTALLED_DIR}")
        elseif(DEFINED ENV{VCPKG_ROOT})
            set(VCPKG_INSTALLED "$ENV{VCPKG_ROOT}/installed")
        endif()

        if(DEFINED VCPKG_INSTALLED AND DEFINED VCPKG_TRIPLET)
            set(VCPKG_DLL_DIR "${VCPKG_INSTALLED}/${VCPKG_TRIPLET}/bin")
            set(VCPKG_DEBUG_DLL_DIR "${VCPKG_INSTALLED}/${VCPKG_TRIPLET}/debug/bin")

            # Use debug DLLs/PDBs for Debug build, release DLLs for Dev build
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(DLL_SOURCE_DIR "${VCPKG_DEBUG_DLL_DIR}")
            else()
                set(DLL_SOURCE_DIR "${VCPKG_DLL_DIR}")
            endif()

            # Define the DLLs and PDBs we need from vcpkg
            # This is faster than xcopy/PowerShell wildcards and more reliable
            set(VCPKG_DLLS_TO_COPY zstd portaudio libsodium)
            set(VCPKG_PDBS_TO_COPY zstd portaudio libsodium)
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                list(APPEND VCPKG_DLLS_TO_COPY mimalloc-debug mimalloc-redirect)
                list(APPEND VCPKG_PDBS_TO_COPY mimalloc-debug.dll)
            else()
                list(APPEND VCPKG_DLLS_TO_COPY mimalloc mimalloc-redirect)
                list(APPEND VCPKG_PDBS_TO_COPY mimalloc.dll)
            endif()

            # Copy DLLs using cmake -E copy_if_different (fast, reliable, no shell startup)
            foreach(DLL_NAME ${VCPKG_DLLS_TO_COPY})
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL_SOURCE_DIR}/${DLL_NAME}.dll" "$<TARGET_FILE_DIR:${TARGET_NAME}>/"
                    COMMENT "Copying ${DLL_NAME}.dll from vcpkg"
                    VERBATIM
                )
            endforeach()

            # Copy PDBs for debugging (only in Debug builds)
            # Note: mimalloc PDBs are named mimalloc-debug.dll.pdb / mimalloc.dll.pdb
            foreach(PDB_NAME ${VCPKG_PDBS_TO_COPY})
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL_SOURCE_DIR}/${PDB_NAME}.pdb" "$<TARGET_FILE_DIR:${TARGET_NAME}>/"
                    COMMENT "Copying ${PDB_NAME}.pdb from vcpkg"
                    VERBATIM
                )
            endforeach()

            message(STATUS "DLL/PDB copying: using ${BoldCyan}cmake -E copy${ColorReset} from ${BoldBlue}${DLL_SOURCE_DIR}${ColorReset}")
        else()
            message(STATUS "DLL/PDB copying: ${BoldYellow}skipped${ColorReset} (vcpkg not configured)")
        endif()
    endif()
endfunction()

# Check if Release build is statically linked (warns if not)
# - Linux: Uses ldd to verify binary has no dynamic dependencies (must be "statically linked")
# - Windows: Checks that only system DLLs are linked (ntdll, kernel32, etc.)
# - macOS: Uses otool to verify only system frameworks are linked
function(check_static_linking TARGET_NAME)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        if(NOT ASCIICHAT_ENFORCE_STATIC_RELEASE)
            message(STATUS "Static linking enforcement disabled for Release builds")
            return()
        endif()
        # Use centralized ASCIICHAT_BASH_EXECUTABLE from FindPrograms.cmake
        if(ASCIICHAT_BASH_EXECUTABLE)
            # Determine platform
            if(UNIX AND NOT APPLE)
                set(PLATFORM "linux")
            elseif(WIN32)
                set(PLATFORM "windows")
            elseif(APPLE)
                set(PLATFORM "macos")
            endif()

            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${ASCIICHAT_BASH_EXECUTABLE} "${CMAKE_SOURCE_DIR}/cmake/utils/check_static_linking.sh" "$<TARGET_FILE:${TARGET_NAME}>" "${PLATFORM}"
                COMMENT "Verifying static linking for Release build"
                VERBATIM
            )
        endif()
    endif()
endfunction()

# Strip symbols in Release builds and clean .comment section
# Note: .comment section is ELF-specific (Linux only)
# Windows uses PE format (no .comment section)
# macOS uses Mach-O format (no .comment section)
# Also ensure no debug info paths are embedded
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Use centralized executables from FindPrograms.cmake
    set(STRIP_EXECUTABLE "${ASCIICHAT_STRIP_EXECUTABLE}")
    set(OBJCOPY_EXECUTABLE "${ASCIICHAT_OBJCOPY_EXECUTABLE}")
    if(STRIP_EXECUTABLE AND OBJCOPY_EXECUTABLE)
        # Only run .comment cleaning on Linux (ELF format)
        # Windows (PE) and macOS (Mach-O) don't have .comment sections
        if(UNIX AND NOT APPLE AND NOT WIN32)
            add_custom_command(TARGET ascii-chat POST_BUILD
                # Clean .comment section BEFORE stripping (remove duplicates, keep version string)
                COMMAND ${CMAKE_SOURCE_DIR}/cmake/utils/clean_comment.sh $<TARGET_FILE:ascii-chat>
                # Strip symbols but keep custom sections
                # Use --strip-debug instead of --strip-unneeded to preserve custom sections
                COMMAND ${STRIP_EXECUTABLE} --strip-debug --keep-section=.comment --keep-section=.ascii_chat --keep-section=.version $<TARGET_FILE:ascii-chat>
                COMMENT "Deduplicating .comment section and stripping debug symbols"
            )
            # Remove embedded file paths from binary using bash script (much faster than PowerShell)
            # Note: DEPENDS is not supported for TARGET form, but the script path is explicit in COMMAND
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND bash "${CMAKE_SOURCE_DIR}/cmake/utils/remove_paths.sh"
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
                # Then remove embedded file paths from binary using bash script (much faster than PowerShell)
                # Use centralized ASCIICHAT_BASH_EXECUTABLE from FindPrograms.cmake
                if(ASCIICHAT_BASH_EXECUTABLE)
                    # Helper function to convert Windows paths to bash compatible format
                    # Auto-detects WSL vs Git Bash/MSYS - the script will do runtime detection
                    # We'll pass Windows paths as-is and let the script handle conversion
                    # This avoids CMake needing to know which bash is being used
                    function(convert_windows_to_wsl_path WINDOWS_PATH OUTPUT_VAR)
                        # Just pass through - the bash script will detect and convert
                        set(${OUTPUT_VAR} "${WINDOWS_PATH}" PARENT_SCOPE)
                    endfunction()

                    # Convert script path
                    if(CMAKE_SOURCE_DIR MATCHES "^[A-Za-z]:")
                        convert_windows_to_wsl_path("${CMAKE_SOURCE_DIR}/cmake/utils/remove_paths.sh" SCRIPT_PATH_BASH)
                    else()
                        set(SCRIPT_PATH_BASH "${CMAKE_SOURCE_DIR}/cmake/utils/remove_paths.sh")
                    endif()

                    # Convert binary path, source dir, and build dir for bash to access files
                    # Note: We need to use generator expressions, so we'll convert in the script itself
                    # But for the script path, we can convert it here
                    # The script will handle path conversion internally for its arguments

                    # Note: DEPENDS is not supported for TARGET form, but the script path is explicit in COMMAND
                    add_custom_command(TARGET ascii-chat POST_BUILD
                        COMMAND ${ASCIICHAT_BASH_EXECUTABLE} "${SCRIPT_PATH_BASH}"
                            "$<TARGET_FILE:ascii-chat>"
                            "${CMAKE_SOURCE_DIR}"
                            "${CMAKE_BINARY_DIR}"
                        COMMENT "Removing embedded file paths from ascii-chat binary"
                    )
                else()
                    message(WARNING "Bash not found (ASCIICHAT_BASH_EXECUTABLE) - cannot remove embedded paths from binary")
                endif()
            else()
                # macOS: strip symbols then codesign (must strip BEFORE codesigning)
                add_custom_command(TARGET ascii-chat POST_BUILD
                    COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat>
                    COMMENT "Stripping symbols from ascii-chat"
                )
                # Code sign after stripping (codesign_target is defined in CodeSigning.cmake)
                codesign_target(ascii-chat)
            endif()
        endif()
    elseif(STRIP_EXECUTABLE)
        # Fallback: just strip symbols if objcopy not available
        if (APPLE)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat>
                COMMENT "Stripping symbols from ascii-chat"
            )
            # Code sign after stripping (codesign_target is defined in CodeSigning.cmake)
            codesign_target(ascii-chat)
        else()
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${STRIP_EXECUTABLE} --strip-all $<TARGET_FILE:ascii-chat>
                COMMENT "Stripping symbols from ascii-chat"
            )
        endif()
    endif()
endif()
