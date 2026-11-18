# =============================================================================
# Recast source transformation dependency
# =============================================================================

include("${CMAKE_SOURCE_DIR}/deps/recast/Recast.cmake")

# Musl is a target toolchain. Recast itself is a host-side source transformer
# and must be built with the runner's native compiler and shared libclang.
if(USE_MUSL)
    if(NOT RECAST_NATIVE_BINARY)
        # Use the same LLVM installation as the main build for the host tool.
        # Do not let PATH select an older clang++ or GCC.
        set(_recast_host_cxx "${ASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE}")
        if(NOT _recast_host_cxx)
            find_program(_recast_host_cxx NAMES clang++-23 clang++ REQUIRED)
        endif()
        # Keep the host-tool cache separate from older GCC/LLVM configurations.
        set(_recast_host_build "${CMAKE_BINARY_DIR}/recast-host-llvm23")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_SOURCE_DIR}/deps/recast"
                -B "${_recast_host_build}" -G Ninja
                "-DRECAST_HOST_CXX=${_recast_host_cxx}"
                "-DCMAKE_C_COMPILER=${ASCIICHAT_CLANG_EXECUTABLE}"
                "-DCMAKE_CXX_COMPILER=${_recast_host_cxx}"
                -DRECAST_BUILD_DOCS=OFF -DBUILD_TESTING=OFF
                -DRECAST_STATIC_LIBCLANG=OFF
            RESULT_VARIABLE _recast_host_configure_result
            OUTPUT_VARIABLE _recast_host_configure_output
            ERROR_VARIABLE _recast_host_configure_error)
        if(NOT _recast_host_configure_result EQUAL 0)
            message(FATAL_ERROR "Failed to configure native Recast host tool:\n${_recast_host_configure_output}\n${_recast_host_configure_error}")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${_recast_host_build}" --target recast
            RESULT_VARIABLE _recast_host_build_result
            OUTPUT_VARIABLE _recast_host_build_output
            ERROR_VARIABLE _recast_host_build_error)
        if(NOT _recast_host_build_result EQUAL 0)
            message(FATAL_ERROR "Failed to build native Recast host tool:\n${_recast_host_build_output}\n${_recast_host_build_error}")
        endif()
        set(RECAST_NATIVE_BINARY "${_recast_host_build}/bin/recast" CACHE FILEPATH
            "Native host Recast executable used for musl source transformation" FORCE)
    endif()
    if(NOT TARGET recast-native)
        add_executable(recast-native IMPORTED GLOBAL)
        set_target_properties(recast-native PROPERTIES
            IMPORTED_LOCATION "${RECAST_NATIVE_BINARY}")
    endif()
else()
    add_subdirectory("${CMAKE_SOURCE_DIR}/deps/recast" "${CMAKE_BINARY_DIR}/recast-build" EXCLUDE_FROM_ALL)
endif()
