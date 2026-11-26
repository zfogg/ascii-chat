# =============================================================================
# Recast source transformation dependency
# =============================================================================

include("${CMAKE_SOURCE_DIR}/deps/recast/Recast.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/utils/HostTools.cmake")
include(ExternalProject)

if(NOT ASCIICHAT_LLVM_DEV_ROOT AND DEFINED ENV{ASCIICHAT_LLVM_DEV_ROOT})
    set(ASCIICHAT_LLVM_DEV_ROOT "$ENV{ASCIICHAT_LLVM_DEV_ROOT}")
endif()
if(MSVC)
    # Native MSVC uses Recast for defer transformation. Keep all Recast
    # discovery and tool paths in this dependency module, not the compiler
    # module; compiler setup should only configure MSVC itself.
    if(NOT ASCIICHAT_LLVM_DEV_ROOT)
        set(ASCIICHAT_LLVM_DEV_ROOT "${ASCIICHAT_DEPS_CACHE_ROOT}/llvm-dev-23.1.3")
    endif()
    set(RECAST_LLVM_DEV_ROOT "${ASCIICHAT_LLVM_DEV_ROOT}" CACHE PATH
        "LLVM development root for the native Recast host" FORCE)
    find_program(_recast_python python3 python REQUIRED)
    find_file(_recast_libclang libclang.dll
        PATHS "$ENV{USERPROFILE}/scoop/apps/llvm/current/bin" "C:/Program Files/LLVM/bin"
        REQUIRED)
    set(RECAST_BACKEND NATIVE CACHE STRING "Recast transformer backend" FORCE)
    set(RECAST_TOOL_ROOT "${CMAKE_SOURCE_DIR}/deps/recast" CACHE PATH
        "Recast source tree" FORCE)
    set(RECAST_PYTHON_COMMAND "${_recast_python}" CACHE FILEPATH
        "Recast Python interpreter" FORCE)
    set(RECAST_LIBCLANG_PATH "${_recast_libclang}" CACHE FILEPATH
        "libclang shared library" FORCE)
elseif(NOT ASCIICHAT_LLVM_DEV_ROOT)
    # Do not retain the Windows fallback in a Unix build tree; Recast must use
    # its normal host LLVM discovery unless an explicit root was supplied.
    unset(RECAST_LLVM_DEV_ROOT CACHE)
    unset(RECAST_LLVM_DEV_ROOT)
endif()

# Musl is a target toolchain. Recast itself is a host-side source transformer
# and must be built with the runner's native compiler and shared libclang.
if(NOT RECAST_NATIVE_BINARY OR NOT EXISTS "${RECAST_NATIVE_BINARY}")
        # Use the same LLVM installation as the main build for the host tool.
        # Do not let PATH select an older clang++ or GCC.
        asciichat_host_toolchain(_recast_host_cc _recast_host_cxx _recast_host_cache)
        # Keep host-tool caches separate across generators. CMake refuses to
        # reuse a build directory configured by a different generator, and
        # developers commonly keep both Visual Studio and Ninja MSVC builds.
        string(MAKE_C_IDENTIFIER "${CMAKE_GENERATOR}" _recast_host_generator)
        set(_recast_host_build
            "${_recast_host_cache}/recast-llvm23-v3-${_recast_host_generator}")
        set(_recast_host_link_flags)
        if(UNIX AND NOT APPLE)
            set(_recast_host_link_flags)
            get_filename_component(_recast_host_llvm_bin
                "${_recast_host_cc}" DIRECTORY)
            set(_recast_host_lld "${_recast_host_llvm_bin}/ld.lld")
            if(NOT EXISTS "${_recast_host_lld}")
                unset(_recast_host_lld CACHE)
                find_program(_recast_host_lld NAMES ld.lld)
            endif()
            if(_recast_host_lld)
                string(APPEND _recast_host_link_flags
                    " -fuse-ld=${_recast_host_lld}")
            endif()
            # Ubuntu's shared libLLVM exposes optional backends whose
            # dependencies are not encoded as transitive linker inputs.
            # Recast is a host tool, so link those host libraries explicitly;
            # these flags never enter the target (musl) build.
            set(_recast_host_system_library_paths
                "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
                "/lib/${CMAKE_LIBRARY_ARCHITECTURE}"
                /usr/lib /lib)
            foreach(_recast_host_link_library IN ITEMS
                ffi z3 z zstd xml2 tinfo icuuc icudata)
                unset(_recast_host_link_path CACHE)
                find_library(_recast_host_link_path
                    NAMES "${_recast_host_link_library}"
                    PATHS ${_recast_host_system_library_paths}
                    NO_DEFAULT_PATH)
                if(_recast_host_link_path)
                    string(APPEND _recast_host_link_flags
                        " ${_recast_host_link_path}")
                endif()
            endforeach()
            foreach(_recast_host_link_library IN ITEMS edit bsd md)
                unset(_recast_host_link_path CACHE)
                find_library(_recast_host_link_path
                    NAMES "${_recast_host_link_library}"
                    PATHS ${_recast_host_system_library_paths}
                    NO_DEFAULT_PATH)
                if(_recast_host_link_path)
                    string(APPEND _recast_host_link_flags
                        " ${_recast_host_link_path}")
                endif()
            endforeach()
        endif()
        set(_recast_host_build_command
            "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --target recast)
        set(_recast_llvm_args)
        if(RECAST_LLVM_DEV_ROOT)
            list(APPEND _recast_llvm_args
                "-DRECAST_LLVM_DEV_ROOT:PATH=${RECAST_LLVM_DEV_ROOT}")
        endif()
        if(CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE)
            list(APPEND _recast_host_build_command --config "${CMAKE_BUILD_TYPE}")
            set(_recast_host_binary
                "${_recast_host_build}/bin/${CMAKE_BUILD_TYPE}/recast${CMAKE_EXECUTABLE_SUFFIX}")
        else()
            set(_recast_host_binary "${_recast_host_build}/bin/recast${CMAKE_EXECUTABLE_SUFFIX}")
        endif()
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
                "-DRECAST_DEPS_CACHE_DIR:PATH=${ASCIICHAT_DEPS_CACHE_ROOT}"
                "-DRECAST_LLVM_DEV_ROOT:PATH=${ASCIICHAT_LLVM_DEV_ROOT}"
                ${_recast_llvm_args}
                -DASCIICHAT_RECAST_HOST_LINK_REV=4
                -DRECAST_BUILD_DOCS=OFF -DBUILD_TESTING=OFF
                -DRECAST_STATIC_LIBCLANG=OFF
            BUILD_COMMAND ${_recast_host_build_command}
            INSTALL_COMMAND ""
            BUILD_BYPRODUCTS "${_recast_host_binary}"
            USES_TERMINAL_CONFIGURE USES_TERMINAL_BUILD)
        ExternalProject_Add_Step(asciichat-recast-host verify-version
            COMMAND "${_recast_host_binary}" --version
            DEPENDEES build
            ALWAYS TRUE
            USES_TERMINAL)
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
