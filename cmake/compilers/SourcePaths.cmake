# Keep release binaries free of local paths, while preserving gcov source lookup.
function(configure_source_path_flags)
    if(NOT ASCIICHAT_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        get_filename_component(_source_dir "${CMAKE_SOURCE_DIR}" ABSOLUTE)
        get_filename_component(_build_dir "${CMAKE_BINARY_DIR}" ABSOLUTE)
        add_compile_options(-ffile-prefix-map=${_source_dir}/=)
        add_compile_options(-ffile-prefix-map=${_build_dir}/=build/)
    endif()
endfunction()
