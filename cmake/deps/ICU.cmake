# ICU common/data supply UTS #46 IDNA. Never use host libraries in cross builds.
include_guard(GLOBAL)
add_library(asciichat-icu INTERFACE)
if(EMSCRIPTEN)
    # The Emscripten port omits data. Stage just its version-matched IDNA resource.
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../scripts/icu_idna_data.py")
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -sUSE_ICU=1 --cflags
        OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
    get_filename_component(_icu_emscripten_dir "${CMAKE_C_COMPILER}" DIRECTORY)
    set(_icu_data_c "${CMAKE_BINARY_DIR}/generated/icu_idna_data.c")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/../scripts/icu_idna_data.py"
        --emscripten "${_icu_emscripten_dir}" --output "${_icu_data_c}"
        COMMAND_ERROR_IS_FATAL ANY)
    add_library(asciichat-icu-data OBJECT "${_icu_data_c}")
    target_link_libraries(asciichat-icu INTERFACE $<TARGET_OBJECTS:asciichat-icu-data>)
    target_compile_options(asciichat-icu INTERFACE -sUSE_ICU=1)
    target_link_options(asciichat-icu INTERFACE -sUSE_ICU=1)
elseif(USE_MUSL OR PLATFORM_IOS)
    include(ExternalProject)
    include(${CMAKE_CURRENT_LIST_DIR}/../toolchains/MuslFlags.cmake)
    find_program(_icu_make NAMES gmake make REQUIRED)
    find_program(_icu_host_cc NAMES clang NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    find_program(_icu_host_cxx NAMES clang++ NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    find_program(_icu_musl_cxx NAMES clang++ NO_CMAKE_FIND_ROOT_PATH REQUIRED)
    set(_icu_cache "${ASCIICHAT_DEPS_CACHE_DIR}/icu")
    set(_icu_source "${_icu_cache}/source")
    set(_icu_host_build "${_icu_cache}/host-build")
    set(_icu_prefix "${_icu_cache}/installed")
    set(_icu_options --enable-static --disable-shared --disable-tests --disable-samples
        --disable-extras --disable-icuio --with-data-packaging=static)
    ExternalProject_Add(asciichat-icu-host
        URL https://github.com/unicode-org/icu/releases/download/release-78.3/icu4c-78.3-sources.tgz
        URL_HASH SHA512=04A49455E1489030C520A4BFD2664FA2171E7938D08F2ACDBBCB1FDA976639FD8B1F0704F2EEC89BA59A7B6D118CEAAB6EC5A096E40D9085A0895D91CE225245
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_DIR "${_icu_source}" BINARY_DIR "${_icu_host_build}"
        PREFIX "${_icu_cache}/host"
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env "CC=${_icu_host_cc}" "CXX=${_icu_host_cxx}"
            "CFLAGS=" "CXXFLAGS=" "LDFLAGS=" "LIBS="
            <SOURCE_DIR>/source/configure ${_icu_options}
        BUILD_COMMAND ${_icu_make} -j8 INSTALL_COMMAND ""
        LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_OUTPUT_ON_FAILURE TRUE)
    if(USE_MUSL)
        asciichat_musl_compile_flags(_icu_cflags _icu_cxxflags)
        set(_icu_cc "${CMAKE_C_COMPILER}")
        set(_icu_cxx "${CMAKE_CXX_COMPILER}")
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            string(APPEND _icu_cc " --target=${MUSL_TARGET}")
            string(APPEND _icu_cxx " --target=${MUSL_TARGET}")
        else()
            # GCC's g++ driver still searches the host glibc C++ include tree,
            # even when its C driver is musl-gcc. Use clang++ for ICU's C++
            # cross objects so libc++ and musl headers stay in one sysroot.
            set(_icu_cxx "${_icu_musl_cxx} --target=${MUSL_TARGET} -stdlib=libc++")
            string(APPEND _icu_cxxflags " -nostdlibinc")
        endif()
        set(_icu_host "${MUSL_TARGET}")
        set(_icu_ldflags "-fuse-ld=lld -static-pie -nostdlib -L${MUSL_LIBDIR} ${MUSL_LIBDIR}/rcrt1.o ${MUSL_LIBDIR}/crti.o")
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            string(APPEND _icu_ldflags " -nostdlib++")
        endif()
        set(_icu_libs "${ALPINE_LIBCXX_STATIC} ${ALPINE_LIBCXXABI_STATIC} ${ALPINE_LIBUNWIND_STATIC} ${MUSL_LIBGCC_PATH} -lc -lm ${MUSL_LIBDIR}/crtn.o")
        get_filename_component(_icu_libcxx_libdir "${ALPINE_LIBCXX_STATIC}" DIRECTORY)
        set(_icu_libcxx_include "${_icu_libcxx_libdir}/../include/c++/v1")
        # GCC's standard library is a host/glibc installation here. ICU's musl
        # artifacts must use the bundled libc++ headers to match the static
        # libc++/libc++abi libraries used by the musl link.
        string(APPEND _icu_cxxflags " -nostdinc++ -isystem ${_icu_libcxx_include}")
    else()
        set(_icu_cc "${CMAKE_C_COMPILER}")
        set(_icu_cxx "${CMAKE_CXX_COMPILER}")
        set(_icu_host "aarch64-apple-darwin")
        set(_icu_cflags "${CMAKE_C_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT} -arch ${CMAKE_OSX_ARCHITECTURES}")
        set(_icu_cxxflags "${CMAKE_CXX_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT} -arch ${CMAKE_OSX_ARCHITECTURES}")
    endif()
    file(MAKE_DIRECTORY "${_icu_prefix}/include")
    ExternalProject_Add(asciichat-icu-cross
        SOURCE_DIR "${_icu_source}" DOWNLOAD_COMMAND ""
        BINARY_DIR "${_icu_cache}/build"
        PREFIX "${_icu_cache}/cross"
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env "CC=${_icu_cc}" "CXX=${_icu_cxx}"
            "CFLAGS=${_icu_cflags}" "CXXFLAGS=${_icu_cxxflags}" "LDFLAGS=${_icu_ldflags}" "LIBS=${_icu_libs}"
            <SOURCE_DIR>/source/configure ${_icu_options} --host=${_icu_host}
            --with-cross-build=${_icu_host_build} --prefix=${_icu_prefix}
        BUILD_COMMAND ${_icu_make} -j8 INSTALL_COMMAND ${_icu_make} install
        INSTALL_BYPRODUCTS "${_icu_prefix}/lib/libicuuc.a" "${_icu_prefix}/lib/libicudata.a"
        DEPENDS asciichat-icu-host
        LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE LOG_OUTPUT_ON_FAILURE TRUE)
    add_dependencies(asciichat-icu asciichat-icu-cross)
    target_include_directories(asciichat-icu INTERFACE "${_icu_prefix}/include")
    target_compile_definitions(asciichat-icu INTERFACE U_STATIC_IMPLEMENTATION)
    target_link_libraries(asciichat-icu INTERFACE "${_icu_prefix}/lib/libicuuc.a" "${_icu_prefix}/lib/libicudata.a")
elseif(UNIX AND NOT APPLE)
    # pkg-config supplies the distro's multiarch library directories even when
    # a newer Clang target is not recognized by CMake's architecture lookup.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ASCIICHAT_ICU REQUIRED IMPORTED_TARGET icu-uc>=60)
    set(ICU_VERSION "${ASCIICHAT_ICU_VERSION}")
    target_link_libraries(asciichat-icu INTERFACE PkgConfig::ASCIICHAT_ICU)
else()
    if(APPLE AND HOMEBREW_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}/opt/icu4c")
    endif()
    if(APPLE AND NOT ASCIICHAT_SHARED_DEPS)
        set(_icu_saved_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
    find_package(ICU 60 REQUIRED COMPONENTS uc data)
    if(DEFINED _icu_saved_suffixes)
        set(CMAKE_FIND_LIBRARY_SUFFIXES "${_icu_saved_suffixes}")
        target_compile_definitions(asciichat-icu INTERFACE U_STATIC_IMPLEMENTATION)
    endif()
    # Match CompilerDetection.cmake: Windows ASan uses the release DLL CRT.
    # ICU's C API does not exchange C++ objects or CRT-owned allocations here.
    if(WIN32 AND CMAKE_BUILD_TYPE MATCHES "^(Debug|Dev)$" AND
       CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL" AND
       NOT VCPKG_TARGET_TRIPLET MATCHES "-static")
        set_property(TARGET ICU::uc PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
        set_property(TARGET ICU::data PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
        set_property(TARGET ICU::uc PROPERTY MAP_IMPORTED_CONFIG_DEV Release)
        set_property(TARGET ICU::data PROPERTY MAP_IMPORTED_CONFIG_DEV Release)
        string(REGEX MATCH "^[0-9]+" _icu_major "${ICU_VERSION}")
        get_filename_component(_icu_libdir "${ICU_UC_LIBRARY_RELEASE}" DIRECTORY)
        get_filename_component(_icu_installed "${_icu_libdir}" DIRECTORY)
        set(ASCIICHAT_ICU_RUNTIME_DLLS
            "${_icu_installed}/bin/icuuc${_icu_major}.dll"
            "${_icu_installed}/bin/icudt${_icu_major}.dll")
    endif()
    target_link_libraries(asciichat-icu INTERFACE ICU::uc ICU::data)
endif()
