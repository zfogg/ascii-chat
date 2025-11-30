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

        if(DEFINED ENV{VCPKG_ROOT} AND DEFINED VCPKG_TRIPLET)
            set(VCPKG_DLL_DIR "$ENV{VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/bin")
            set(VCPKG_DEBUG_DLL_DIR "$ENV{VCPKG_ROOT}/installed/${VCPKG_TRIPLET}/debug/bin")

            # Use debug DLLs/PDBs for Debug build, release DLLs for Dev build
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                set(DLL_SOURCE_DIR "${VCPKG_DEBUG_DLL_DIR}")
            else()
                set(DLL_SOURCE_DIR "${VCPKG_DLL_DIR}")
            endif()

            # Convert to native path (backslashes for Windows cmd.exe)
            file(TO_NATIVE_PATH "${DLL_SOURCE_DIR}" DLL_SOURCE_DIR_NATIVE)

            # Copy all DLLs from vcpkg bin directory to output bin using cmd /c xcopy
            # /D = only copy if source is newer than destination
            # /Y = suppress prompts
            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND cmd /c "if exist \"${DLL_SOURCE_DIR_NATIVE}\\*.dll\" xcopy /D /Y \"${DLL_SOURCE_DIR_NATIVE}\\*.dll\" \"$<TARGET_FILE_DIR:${TARGET_NAME}>\\\""
                COMMENT "Copying DLLs from vcpkg to bin directory"
                VERBATIM
            )

            # Copy only the PDBs we actually need for debugging (not all vcpkg PDBs)
            # This keeps the build directory smaller and avoids copying unrelated PDBs
            # Use cmake -E copy_if_different for proper path handling (generator expressions output forward slashes)
            # Note: mimalloc PDBs are named mimalloc-debug.dll.pdb / mimalloc.dll.pdb (not just .pdb)
            set(VCPKG_PDBS_TO_COPY zstd portaudio libsodium)
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                list(APPEND VCPKG_PDBS_TO_COPY mimalloc-debug.dll)
            else()
                list(APPEND VCPKG_PDBS_TO_COPY mimalloc.dll)
            endif()

            # Always add copy commands - they run at build time, not configure time
            # copy_if_different will skip if source doesn't exist (no error)
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
        find_program(BASH_EXECUTABLE bash HINTS "C:/Program Files/Git/usr/bin")
        if(BASH_EXECUTABLE)
            # Determine platform
            if(UNIX AND NOT APPLE)
                set(PLATFORM "linux")
            elseif(WIN32)
                set(PLATFORM "windows")
            elseif(APPLE)
                set(PLATFORM "macos")
            endif()

            add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${BASH_EXECUTABLE} "${CMAKE_SOURCE_DIR}/cmake/utils/check_static_linking.sh" "$<TARGET_FILE:${TARGET_NAME}>" "${PLATFORM}"
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
    find_program(STRIP_EXECUTABLE strip)
    find_program(OBJCOPY_EXECUTABLE objcopy)
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
                # On Windows, prefer Git Bash over WSL bash for consistent path handling
                if(WIN32)
                    find_program(BASH_EXECUTABLE
                        NAMES bash
                        PATHS
                            "C:/Program Files/Git/usr/bin"
                            "C:/Program Files/Git/bin"
                            "$ENV{ProgramFiles}/Git/usr/bin"
                            "$ENV{ProgramFiles}/Git/bin"
                        NO_DEFAULT_PATH
                    )
                    # If Git Bash not found in standard locations, fall back to any bash
                    if(NOT BASH_EXECUTABLE)
                        find_program(BASH_EXECUTABLE bash)
                    endif()
                else()
                    find_program(BASH_EXECUTABLE bash)
                endif()
                if(BASH_EXECUTABLE)
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
                        COMMAND ${BASH_EXECUTABLE} "${SCRIPT_PATH_BASH}"
                            "$<TARGET_FILE:ascii-chat>"
                            "${CMAKE_SOURCE_DIR}"
                            "${CMAKE_BINARY_DIR}"
                        COMMENT "Removing embedded file paths from ascii-chat binary"
                    )
                else()
                    message(WARNING "Bash not found - cannot remove embedded paths from binary")
                endif()
            else()
                # macOS: strip symbols
                add_custom_command(TARGET ascii-chat POST_BUILD
                    COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat>
                    COMMENT "Stripping symbols from ascii-chat"
                )
            endif()
        endif()
    elseif(STRIP_EXECUTABLE)
        # Fallback: just strip symbols if objcopy not available
        if (APPLE)
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${STRIP_EXECUTABLE} $<TARGET_FILE:ascii-chat>
                COMMENT "Stripping symbols from ascii-chat"
            )
        else()
            add_custom_command(TARGET ascii-chat POST_BUILD
                COMMAND ${STRIP_EXECUTABLE} --strip-all $<TARGET_FILE:ascii-chat>
                COMMENT "Stripping symbols from ascii-chat"
            )
        endif()
    endif()
endif()
