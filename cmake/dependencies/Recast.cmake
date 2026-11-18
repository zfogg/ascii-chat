# =============================================================================
# Recast source transformation dependency
# =============================================================================

include("${CMAKE_SOURCE_DIR}/deps/recast/Recast.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/utils/HostTools.cmake")
include(ExternalProject)

# Musl is a target toolchain. Recast itself is a host-side source transformer
# and must be built with the runner's native compiler and shared libclang.
if(NOT RECAST_NATIVE_BINARY OR NOT EXISTS "${RECAST_NATIVE_BINARY}")
        # Use the same LLVM installation as the main build for the host tool.
        # Do not let PATH select an older clang++ or GCC.
        asciichat_host_toolchain(_recast_host_cc _recast_host_cxx _recast_host_cache)
        # Keep the host-tool cache separate from older GCC/LLVM configurations.
        set(_recast_host_build "${_recast_host_cache}/recast-llvm23")
        set(_recast_host_link_flags)
        if(UNIX AND NOT APPLE)
            # Ubuntu's shared libLLVM exposes optional backends whose
            # dependencies are not encoded as transitive linker inputs.
            # Recast is a host tool, so link those host libraries explicitly;
            # these flags never enter the target (musl) build.
            set(_recast_host_link_flags
                "-lffi /lib/x86_64-linux-gnu/libedit.so.2 /lib/x86_64-linux-gnu/libbsd.so.0 /lib/x86_64-linux-gnu/libmd.so.0 -lz3 -lz -lzstd -lxml2 -ltinfo -licuuc -licudata")
        endif()
        set(_recast_host_binary "${_recast_host_build}/bin/recast${CMAKE_EXECUTABLE_SUFFIX}")
        ExternalProject_Add(asciichat-recast-host
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/deps/recast"
            BINARY_DIR "${_recast_host_build}"
            PREFIX "${_recast_host_cache}/recast-host"
            CMAKE_ARGS
                -G Ninja
                "-DRECAST_HOST_CXX=${_recast_host_cxx}"
                "-DCMAKE_C_COMPILER=${_recast_host_cc}"
                "-DCMAKE_CXX_COMPILER=${_recast_host_cxx}"
                "-DCMAKE_EXE_LINKER_FLAGS=${_recast_host_link_flags}"
                -DRECAST_BUILD_DOCS=OFF -DBUILD_TESTING=OFF
                -DRECAST_STATIC_LIBCLANG=OFF
            BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target recast
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${_recast_host_binary}"
            USES_TERMINAL_CONFIGURE USES_TERMINAL_BUILD)
        set(RECAST_NATIVE_BINARY "${_recast_host_binary}" CACHE FILEPATH
            "Native host Recast executable used for musl source transformation" FORCE)
endif()
if(NOT TARGET recast-native)
    add_executable(recast-native IMPORTED GLOBAL)
    set_target_properties(recast-native PROPERTIES
        IMPORTED_LOCATION "${RECAST_NATIVE_BINARY}")
    if(TARGET asciichat-recast-host)
        add_dependencies(recast-native asciichat-recast-host)
    endif()
endif()
