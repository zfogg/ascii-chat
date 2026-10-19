# Upstream libvterm is Make-based; use a small target wrapper and tracked generators.
if(PLATFORM_IOS AND NOT BUILD_EXECUTABLES)
    return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../utils/SourceDependency.cmake)
if(EMSCRIPTEN OR WIN32 OR USE_MUSL OR PLATFORM_IOS OR ASCIICHAT_BUILD_DEPS_FROM_SOURCE)
    function(asciichat_build_vterm)
        asciichat_dependency_defaults()
        FetchContent_Declare(libvterm-src
            GIT_REPOSITORY https://github.com/neovim/libvterm.git
            GIT_TAG v0.3.3 GIT_SHALLOW TRUE)
        find_package(Perl REQUIRED)
        FetchContent_MakeAvailable(libvterm-src)
        set(_vterm_source "${libvterm-src_SOURCE_DIR}")
        set(_vterm_binary "${CMAKE_CURRENT_BINARY_DIR}/_deps/libvterm")
        file(MAKE_DIRECTORY "${_vterm_binary}/encoding")
        file(GLOB _tables CONFIGURE_DEPENDS "${_vterm_source}/src/encoding/*.tbl")
        set(_generated)
        foreach(_table IN LISTS _tables)
            get_filename_component(_name "${_table}" NAME_WE)
            set(_output "${_vterm_binary}/encoding/${_name}.inc")
            add_custom_command(OUTPUT "${_output}"
                COMMAND "${CMAKE_COMMAND}" "-DPERL=${PERL_EXECUTABLE}"
                    "-DGENERATOR=${_vterm_source}/tbl2inc_c.pl" "-DINPUT=${_table}" "-DOUTPUT=${_output}"
                    -P "${CMAKE_CURRENT_LIST_DIR}/../scripts/GenerateVtermEncoding.cmake"
                DEPENDS "${_table}" "${_vterm_source}/tbl2inc_c.pl"
                    "${CMAKE_CURRENT_LIST_DIR}/../scripts/GenerateVtermEncoding.cmake" VERBATIM)
            list(APPEND _generated "${_output}")
        endforeach()
        file(GLOB _sources CONFIGURE_DEPENDS "${_vterm_source}/src/*.c")
        # Compile encoding.c beside the generated tables, even with an old source cache.
        list(REMOVE_ITEM _sources "${_vterm_source}/src/encoding.c")
        configure_file("${_vterm_source}/src/encoding.c" "${_vterm_binary}/encoding.c" COPYONLY)
        add_library(asciichat-vterm ${_sources} "${_vterm_binary}/encoding.c" ${_generated})
        target_include_directories(asciichat-vterm PUBLIC "${_vterm_source}/include"
            PRIVATE "${_vterm_source}/src" "${_vterm_binary}")
        if(WIN32)
            # Upstream uses the portable C string APIs, not Microsoft's secure CRT variants.
            target_compile_definitions(asciichat-vterm PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        if(EMSCRIPTEN)
            target_compile_options(asciichat-vterm PRIVATE -msimd128)
        endif()
        if(USE_MUSL)
            target_compile_options(asciichat-vterm PRIVATE -nostdlibinc "--target=${MUSL_TARGET}")
            target_include_directories(asciichat-vterm SYSTEM PRIVATE "${MUSL_INCLUDE_DIR}" "${KERNEL_HEADERS_DIR}")
        endif()
        set_target_properties(asciichat-vterm PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

        set(VTERM_INCLUDE_DIRS "${_vterm_source}/include" PARENT_SCOPE)
    endfunction()
    asciichat_build_vterm()
    set(VTERM_LDFLAGS asciichat-vterm)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
    set(VTERM_LDFLAGS PkgConfig::VTERM)
endif()
set(RENDER_FILE_LIBS ${VTERM_LDFLAGS} ${FREETYPE_LIBRARIES} ${FONTCONFIG_LDFLAGS})
set(RENDER_FILE_INCLUDES ${VTERM_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS} ${FONTCONFIG_INCLUDE_DIRS})
set(GHOSTTY_LIBS ${RENDER_FILE_LIBS})
set(GHOSTTY_INCLUDES ${RENDER_FILE_INCLUDES})
