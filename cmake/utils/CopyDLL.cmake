include_guard(GLOBAL)

# Copy a DLL to the build bin directory
#
# Usage:
#   copy_dll(
#       NAME liblzma.dll
#       HINTS "${VCPKG_BIN}" "${LLVM_BIN}"
#       DEST "${CMAKE_BINARY_DIR}/bin"
#       COMMENT "for query tool"
#       DEPENDS_TARGET ascii-query-server-copy  # optional
#   )
#
# Parameters:
#   NAME           - DLL filename (e.g., liblzma.dll)
#   HINTS          - List of directories to search for the DLL
#   DEST           - Destination directory
#   COMMENT        - Description for the copy operation (optional)
#   DEPENDS_TARGET - Target that should depend on this copy (optional)
#
# Creates a custom target named ${NAME}-copy (with . and - replaced by _)
function(copy_dll)
    cmake_parse_arguments(ARG "" "NAME;DEST;COMMENT;DEPENDS_TARGET" "HINTS" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "copy_dll: NAME is required")
    endif()
    if(NOT ARG_HINTS)
        message(FATAL_ERROR "copy_dll: HINTS is required")
    endif()
    if(NOT ARG_DEST)
        message(FATAL_ERROR "copy_dll: DEST is required")
    endif()

    # Find the DLL in hint directories
    set(_dll_src "")
    foreach(_hint IN LISTS ARG_HINTS)
        if(EXISTS "${_hint}/${ARG_NAME}")
            set(_dll_src "${_hint}/${ARG_NAME}")
            break()
        endif()
    endforeach()

    if(NOT _dll_src)
        return()
    endif()

    # Create target name from DLL name (replace . and - with _)
    string(REPLACE "." "_" _target_name "${ARG_NAME}")
    string(REPLACE "-" "_" _target_name "${_target_name}")
    set(_target_name "${_target_name}-copy")

    set(_dll_dest "${ARG_DEST}/${ARG_NAME}")

    # Build comment
    if(ARG_COMMENT)
        set(_comment "Copying ${ARG_NAME} ${ARG_COMMENT}")
    else()
        set(_comment "Copying ${ARG_NAME}")
    endif()

    add_custom_command(
        OUTPUT "${_dll_dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll_src}" "${_dll_dest}"
        DEPENDS "${_dll_src}"
        COMMENT "${_comment}"
        VERBATIM
    )

    add_custom_target(${_target_name} ALL DEPENDS "${_dll_dest}")

    if(ARG_DEPENDS_TARGET AND TARGET ${ARG_DEPENDS_TARGET})
        add_dependencies(${ARG_DEPENDS_TARGET} ${_target_name})
    endif()

    message(STATUS "DLL copy: ${ARG_NAME} from ${_dll_src}")
endfunction()

# Copy multiple DLLs with the same hints and destination
#
# Usage:
#   copy_dlls(
#       NAMES liblzma.dll libxml2.dll zlib1.dll
#       HINTS "${VCPKG_BIN}"
#       DEST "${CMAKE_BINARY_DIR}/bin"
#       COMMENT "for query tool"
#       DEPENDS_TARGET ascii-query-server-copy
#   )
function(copy_dlls)
    cmake_parse_arguments(ARG "" "DEST;COMMENT;DEPENDS_TARGET" "NAMES;HINTS" ${ARGN})

    foreach(_name IN LISTS ARG_NAMES)
        copy_dll(
            NAME "${_name}"
            HINTS ${ARG_HINTS}
            DEST "${ARG_DEST}"
            COMMENT "${ARG_COMMENT}"
            DEPENDS_TARGET "${ARG_DEPENDS_TARGET}"
        )
    endforeach()
endfunction()

# Stage DLLs as incremental outputs required by a target
#
# Usage:
#   stage_dlls_for_target(
#       TARGET ascii-chat
#       NAMES zstd.dll portaudio.dll libsodium.dll
#       SOURCE_DIR "${VCPKG_BIN}"
#       COMMENT "from vcpkg"
#   )
#
# Parameters:
#   TARGET     - Target that requires the staged runtime files
#   NAMES      - List of DLL filenames (without path)
#   SOURCE_DIR - Directory containing the DLLs
#   COMMENT    - Description suffix (optional)
function(stage_dlls_for_target)
    cmake_parse_arguments(ARG "" "TARGET;SOURCE_DIR;COMMENT" "NAMES" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "stage_dlls_for_target: TARGET is required")
    endif()
    if(NOT ARG_NAMES)
        message(FATAL_ERROR "stage_dlls_for_target: NAMES is required")
    endif()
    if(NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "stage_dlls_for_target: SOURCE_DIR is required")
    endif()

    if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        message(FATAL_ERROR "stage_dlls_for_target: CMAKE_RUNTIME_OUTPUT_DIRECTORY is required")
    endif()

    set(_sources "")
    set(_outputs "")
    foreach(_name IN LISTS ARG_NAMES)
        list(APPEND _sources "${ARG_SOURCE_DIR}/${_name}")
        list(APPEND _outputs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_name}")
    endforeach()

    list(LENGTH ARG_NAMES _dll_count)
    if(ARG_COMMENT)
        set(_comment "Staging ${_dll_count} runtime files ${ARG_COMMENT}")
    else()
        set(_comment "Staging ${_dll_count} runtime files")
    endif()

    # Use one process for the directory batch. Declared outputs also avoid
    # rechecking unchanged runtime files after every executable relink.
    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sources} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
        COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${_outputs}
        DEPENDS ${_sources}
        COMMENT "${_comment}"
        VERBATIM
    )

    string(MD5 _copy_id "${ARG_TARGET};${ARG_SOURCE_DIR};${ARG_NAMES}")
    set(_copy_target "${ARG_TARGET}-stage-runtime-${_copy_id}")
    add_custom_target(${_copy_target} DEPENDS ${_outputs})
    add_dependencies(${ARG_TARGET} ${_copy_target})
endfunction()
