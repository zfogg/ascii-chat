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

# Copy DLLs as a POST_BUILD step for a target
#
# Usage:
#   copy_dlls_post_build(
#       TARGET ascii-chat
#       NAMES zstd.dll portaudio.dll libsodium.dll
#       SOURCE_DIR "${VCPKG_BIN}"
#       COMMENT "from vcpkg"
#   )
#
# Parameters:
#   TARGET     - Target to attach POST_BUILD commands to
#   NAMES      - List of DLL filenames (without path)
#   SOURCE_DIR - Directory containing the DLLs
#   COMMENT    - Description suffix (optional)
function(copy_dlls_post_build)
    cmake_parse_arguments(ARG "" "TARGET;SOURCE_DIR;COMMENT" "NAMES" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "copy_dlls_post_build: TARGET is required")
    endif()
    if(NOT ARG_NAMES)
        message(FATAL_ERROR "copy_dlls_post_build: NAMES is required")
    endif()
    if(NOT ARG_SOURCE_DIR)
        message(FATAL_ERROR "copy_dlls_post_build: SOURCE_DIR is required")
    endif()

    foreach(_name IN LISTS ARG_NAMES)
        if(ARG_COMMENT)
            set(_comment "Copying ${_name} ${ARG_COMMENT}")
        else()
            set(_comment "Copying ${_name}")
        endif()

        add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ARG_SOURCE_DIR}/${_name}"
                "$<TARGET_FILE_DIR:${ARG_TARGET}>/"
            COMMENT "${_comment}"
            VERBATIM
        )
    endforeach()
endfunction()
