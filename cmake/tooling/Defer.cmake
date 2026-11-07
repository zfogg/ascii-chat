include_guard(GLOBAL)

# Configure after targets are created so source replacement affects the actual targets.
set(ASCIICHAT_LIBCLANG_PATH "" CACHE FILEPATH "LLVM 23 libclang shared library")

function(ascii_defer_apply_target target)
    get_target_property(_type "${target}" TYPE)
    if(NOT _type MATCHES "^(OBJECT_LIBRARY|STATIC_LIBRARY|SHARED_LIBRARY|EXECUTABLE)$")
        return()
    endif()
    get_target_property(_sources "${target}" SOURCES)
    get_target_property(_source_dir "${target}" SOURCE_DIR)
    get_target_property(_prerequisites "${target}" MANUALLY_ADDED_DEPENDENCIES)
    if(NOT _sources)
        return()
    endif()
    set(_new_sources "")
    foreach(_source IN LISTS _sources)
        if(_source MATCHES "\\$<" OR NOT _source MATCHES "\\.c$")
            list(APPEND _new_sources "${_source}")
            continue()
        endif()
        get_filename_component(_absolute "${_source}" ABSOLUTE BASE_DIR "${_source_dir}")
        file(RELATIVE_PATH _relative "${CMAKE_SOURCE_DIR}" "${_absolute}")
        if(NOT _relative MATCHES "^(lib|src|tests)/" OR _relative MATCHES "^src/tooling/" OR NOT EXISTS "${_absolute}")
            list(APPEND _new_sources "${_source}")
            continue()
        endif()
        # Rescan on source changes, including when the first defer is added.
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_absolute}")
        file(READ "${_absolute}" _source_text)
        if(NOT _source_text MATCHES "(^|[^a-zA-Z0-9_])defer[ \t\r\n]*\\(")
            list(APPEND _new_sources "${_source}")
            continue()
        endif()
        set(_generated "${CMAKE_BINARY_DIR}/defer_transformed/${target}/${_relative}")
        set(_depfile "${_generated}.d")
        get_filename_component(_output_dir "${_generated}" DIRECTORY)
        get_filename_component(_original_dir "${_absolute}" DIRECTORY)
        add_custom_command(
            OUTPUT "${_generated}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_output_dir}"
            COMMAND "${ASCIICHAT_PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/src/tooling/defer/defer.py"
                --libclang "${ASCIICHAT_LIBCLANG_PATH}"
                --compilation-database "${CMAKE_BINARY_DIR}"
                --depfile "${_depfile}"
                "${_absolute}" "${_generated}"
            DEPENDS "${_absolute}" "${CMAKE_SOURCE_DIR}/src/tooling/defer/defer.py"
                "${CMAKE_SOURCE_DIR}/src/tooling/defer/vendor/clang/cindex.py"
                "${CMAKE_BINARY_DIR}/compile_commands.json"
            DEPFILE "${_depfile}"
            COMMENT "Transforming defer statements in ${_relative}"
            VERBATIM
        )
        # The target can live in a subdirectory while this command is defined
        # here. An explicit target dependency makes the generator rule visible
        # across directory scopes.
        string(MD5 _defer_id "${target}|${_absolute}")
        add_custom_target("ascii-defer-${_defer_id}" DEPENDS "${_generated}")
        if(_prerequisites)
            add_dependencies("ascii-defer-${_defer_id}" ${_prerequisites})
        endif()
        if(TARGET generate_version AND NOT "generate_version" IN_LIST _prerequisites)
            add_dependencies("ascii-defer-${_defer_id}" generate_version)
        endif()
        add_dependencies("${target}" "ascii-defer-${_defer_id}")
        set_source_files_properties("${_generated}" TARGET_DIRECTORY "${target}"
            PROPERTIES GENERATED TRUE SKIP_UNITY_BUILD_INCLUSION TRUE
            INCLUDE_DIRECTORIES "${_original_dir}")
        list(APPEND _new_sources "${_generated}")
    endforeach()
    set_property(TARGET "${target}" PROPERTY SOURCES "${_new_sources}")
endfunction()

function(ascii_defer_prepare)
    if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
        message(FATAL_ERROR "defer requires CMAKE_EXPORT_COMPILE_COMMANDS=ON")
    endif()
    if(ASCIICHAT_BUILD_WITH_PANIC)
        message(FATAL_ERROR "defer and panic instrumentation cannot yet be combined")
    endif()
    if(NOT ASCIICHAT_PYTHON3_EXECUTABLE)
        message(FATAL_ERROR "defer requires Python 3")
    endif()
    if(NOT ASCIICHAT_LIBCLANG_PATH)
        get_filename_component(_compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(_compiler_real "${CMAKE_C_COMPILER}" REALPATH)
        get_filename_component(_compiler_real_dir "${_compiler_real}" DIRECTORY)
        if(WIN32)
            set(_libclang_names libclang.dll)
        elseif(APPLE)
            set(_libclang_names libclang.dylib)
        else()
            set(_libclang_names libclang.so libclang.so.23)
        endif()
        find_file(_libclang NAMES ${_libclang_names}
            HINTS "${_compiler_dir}" "${_compiler_dir}/../lib"
                  "${_compiler_real_dir}" "${_compiler_real_dir}/../lib"
            NO_CACHE)
        if(NOT _libclang)
            message(FATAL_ERROR "defer requires LLVM 23 libclang; set ASCIICHAT_LIBCLANG_PATH")
        endif()
        set(ASCIICHAT_LIBCLANG_PATH "${_libclang}" CACHE FILEPATH "LLVM 23 libclang shared library" FORCE)
    endif()
    execute_process(
        COMMAND "${ASCIICHAT_PYTHON3_EXECUTABLE}" -c
            "import sys; assert sys.version_info >= (3, 10), 'Python 3.10 or newer is required'; sys.path.insert(0, sys.argv[1]); from clang import cindex; cindex.Config.set_library_file(sys.argv[2]); print(cindex.conf.get_clang_version())"
            "${CMAKE_SOURCE_DIR}/src/tooling/defer/vendor" "${ASCIICHAT_LIBCLANG_PATH}"
        RESULT_VARIABLE _clang_check_result OUTPUT_VARIABLE _clang_check_output ERROR_VARIABLE _clang_check_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _clang_check_result EQUAL 0 OR NOT _clang_check_output MATCHES "clang version 23\\.")
        message(FATAL_ERROR "defer requires Python 3.10+ and LLVM 23 libclang at ASCIICHAT_LIBCLANG_PATH. ${_clang_check_output} ${_clang_check_error}")
    endif()
    get_property(_root_targets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_src_targets DIRECTORY "${CMAKE_SOURCE_DIR}/src" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _root_targets _src_targets)
        if(_target MATCHES "^ascii-chat(-|$)")
            ascii_defer_apply_target("${_target}")
        endif()
    endforeach()
endfunction()

function(ascii_defer_prepare_tests)
    get_property(_test_targets DIRECTORY "${CMAKE_SOURCE_DIR}/tests" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _test_targets)
        ascii_defer_apply_target("${_target}")
    endforeach()
endfunction()
