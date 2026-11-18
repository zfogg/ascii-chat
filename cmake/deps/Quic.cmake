# ngtcp2's OpenSSL QUIC TLS backend requires the mainline QUIC TLS API. Probe
# the selected headers and linked SSL library, not the version of an openssl
# executable that might come from a different installation.
include(CheckSymbolExists)
include(CMakePushCheckState)

function(asciichat_check_quic_tls)
    if(NOT TARGET OpenSSL::SSL)
        message(FATAL_ERROR "QUIC requires the selected OpenSSL::SSL target")
    endif()

    cmake_push_check_state(RESET)
    # OpenSSL::SSL may not propagate its crypto dependency while CMake is
    # probing a static QUIC link. Require both selected targets explicitly so
    # the probe validates the same libraries used by the final executable.
    set(CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS})
    if(TARGET Threads::Threads)
        list(APPEND CMAKE_REQUIRED_LIBRARIES Threads::Threads)
    endif()
    set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
    unset(ASCIICHAT_OPENSSL_QUIC_TLS_LINKS CACHE)
    check_symbol_exists(SSL_set_quic_tls_cbs "openssl/ssl.h" ASCIICHAT_OPENSSL_QUIC_TLS_LINKS)
    cmake_pop_check_state()

    if(NOT ASCIICHAT_OPENSSL_QUIC_TLS_LINKS)
        message(FATAL_ERROR
            "QUIC requires OpenSSL 3.6.0 or newer with the linked SSL_set_quic_tls_cbs API. "
            "Check the selected OpenSSL headers, libraries, and CMake toolchain.")
    endif()
endfunction()

function(asciichat_configure_quic)
    asciichat_check_quic_tls()

    # The two upstream projects both define a global `check` target. Configure
    # them in separate CMake builds so their target names and options cannot collide
    # with each other or with the parent project.
    include(ExternalProject)

    if(USE_MUSL OR ASCIICHAT_STATIC_DEPS)
        set(_quic_shared OFF)
    else()
        set(_quic_shared ${BUILD_SHARED_LIBS})
    endif()
    if(_quic_shared)
        set(_quic_shared_type SHARED)
        set(_quic_library_prefix "${CMAKE_SHARED_LIBRARY_PREFIX}")
        set(_quic_library_suffix "${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_quic_ngtcp2_target ngtcp2)
        set(_quic_ngtcp2_crypto_target ngtcp2_crypto_ossl)
        set(_quic_nghttp3_target nghttp3)
        set(_quic_enable_shared ON)
        set(_quic_enable_static OFF)
    else()
        set(_quic_shared_type STATIC)
        set(_quic_library_prefix "${CMAKE_STATIC_LIBRARY_PREFIX}")
        set(_quic_library_suffix "${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set(_quic_ngtcp2_target ngtcp2_static)
        set(_quic_ngtcp2_crypto_target ngtcp2_crypto_ossl_static)
        set(_quic_nghttp3_target nghttp3_static)
        set(_quic_enable_shared OFF)
        set(_quic_enable_static ON)
    endif()
    message(STATUS "QUIC dependency libraries: ${_quic_shared}")

    set(_quic_common_args
        "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_PROJECT_INCLUDE:FILEPATH=${CMAKE_SOURCE_DIR}/cmake/deps/QuicExternalSetup.cmake"
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
        "-DCMAKE_REQUIRED_INCLUDES:STRING=${OPENSSL_INCLUDE_DIR}"
        "-DHAVE_SSL_PROVIDE_QUIC_DATA:BOOL=OFF"
        "-DHAVE_SSL_SET_QUIC_TLS_CBS:BOOL=ON"
        "-DENABLE_LIB_ONLY:BOOL=ON"
        "-DENABLE_STATIC_LIB:BOOL=${_quic_enable_static}"
        "-DENABLE_SHARED_LIB:BOOL=${_quic_enable_shared}"
        "-DBUILD_TESTING:BOOL=OFF")
    # Keep the isolated ngtcp2 configure aligned with the parent dependency
    # configuration.  In particular, debug/shared builds must not reuse a
    # stale static OpenSSL selection from an earlier configure.
    if(_quic_shared)
        list(APPEND _quic_common_args "-DOPENSSL_USE_STATIC_LIBS:BOOL=OFF")
    else()
        list(APPEND _quic_common_args "-DOPENSSL_USE_STATIC_LIBS:BOOL=ON")
    endif()
    if(WIN32)
        # The isolated ngtcp2 project links its OpenSSL-backed DLL directly;
        # unlike the parent target it does not inherit the Windows system
        # libraries from asciichat-quic's interface link requirements.
        list(APPEND _quic_common_args
            "-DCMAKE_SHARED_LINKER_FLAGS_RELEASE:STRING=/DEFAULTLIB:ws2_32.lib /DEFAULTLIB:crypt32.lib"
            "-DCMAKE_SHARED_LINKER_FLAGS_DEBUG:STRING=/DEFAULTLIB:ws2_32.lib /DEFAULTLIB:crypt32.lib"
            "-DCMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO:STRING=/DEFAULTLIB:ws2_32.lib /DEFAULTLIB:crypt32.lib"
            "-DCMAKE_SHARED_LINKER_FLAGS_MINSIZEREL:STRING=/DEFAULTLIB:ws2_32.lib /DEFAULTLIB:crypt32.lib")
    endif()
    # The parent vcpkg toolchain must not be inherited by these isolated
    # projects: it can redirect them to VCPKG_ROOT/installed and ignore the
    # restored cache paths. Their required OpenSSL paths are passed explicitly
    # below, so they remain independent of package-manager mutation.
    foreach(_variable CMAKE_SYSROOT CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES
                      CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_C_COMPILER_TARGET CMAKE_MSVC_RUNTIME_LIBRARY
                      CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
                      CMAKE_C_FLAGS_MINSIZEREL CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                      CMAKE_MODULE_LINKER_FLAGS VCPKG_INSTALLED_DIR VCPKG_TARGET_TRIPLET)
        if(DEFINED ${_variable} AND NOT "${${_variable}}" STREQUAL "")
            list(APPEND _quic_common_args "-D${_variable}:STRING=${${_variable}}")
        endif()
    endforeach()
    # Explicitly clear a previously cached parent toolchain in a reused
    # generator-specific directory; ngtcp2/nghttp3 receive their dependency
    # locations through the arguments below and do not need vcpkg's toolchain.
    list(APPEND _quic_common_args "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=")
    if(USE_MUSL)
        list(APPEND _quic_common_args
            "-DCMAKE_TRY_COMPILE_TARGET_TYPE:STRING=STATIC_LIBRARY"
            "-DCMAKE_C_FLAGS:STRING=${CMAKE_C_FLAGS} -pthread"
            "-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} -pthread"
            "-DCMAKE_EXE_LINKER_FLAGS:STRING=${CMAKE_EXE_LINKER_FLAGS} -pthread"
            "-DCMAKE_SHARED_LINKER_FLAGS:STRING=${CMAKE_SHARED_LINKER_FLAGS} -pthread"
            "-DCMAKE_HAVE_LIBC_PTHREAD:INTERNAL=1"
            "-DCMAKE_USE_PTHREADS_INIT:INTERNAL=1"
            "-DCMAKE_THREAD_LIBS_INIT:STRING=-pthread")
        foreach(_variable ASCIICHAT_DEPS_CACHE_DIR ASCIICHAT_MUSL_SYSROOT MUSL_TARGET MUSL_INCLUDE_DIR MUSL_LIBDIR
                          KERNEL_HEADERS_DIR)
            if(DEFINED ${_variable} AND NOT "${${_variable}}" STREQUAL "")
                list(APPEND _quic_common_args "-D${_variable}:STRING=${${_variable}}")
            endif()
        endforeach()
    endif()
    if(CMAKE_CROSSCOMPILING)
        list(APPEND _quic_common_args "-DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}"
                                      "-DCMAKE_SYSTEM_PROCESSOR:STRING=${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    # The ngtcp2 configure is a separate CMake process. Generator expressions
    # such as $<TARGET_FILE:OpenSSL::SSL> are only valid in the parent build's
    # generated buildsystem and are rejected by CMake 4.5 while ngtcp2 runs
    # try_compile checks. Pass the already-resolved FindOpenSSL paths instead.
    # Prefer the selected parent targets over the legacy FindOpenSSL cache
    # variables.  vcpkg can leave x64-windows-static paths in those variables
    # after a previous configure even though the parent selected shared
    # x64-windows libraries.
    if(WIN32)
        # vcpkg's debug import libraries are the authoritative shared Windows
        # paths; imported-target properties can retain a stale path.  For the
        # static triplet, pass the release import libraries explicitly too;
        # OPENSSL_SSL_LIBRARY may otherwise be a CMake optimized/debug list,
        # which cannot be represented by a child FILEPATH cache entry.
        set(_quic_vcpkg_root "${ASCIICHAT_DEPS_CACHE_ROOT}/vcpkg")
        set(_quic_vcpkg_debug_libdir
            "${_quic_vcpkg_root}/${VCPKG_TARGET_TRIPLET}/debug/lib")
        if(_quic_shared)
            set(_quic_ssl_file "${_quic_vcpkg_debug_libdir}/libssl.lib")
            set(_quic_crypto_file "${_quic_vcpkg_debug_libdir}/libcrypto.lib")
        else()
            set(_quic_vcpkg_libdir
                "${_quic_vcpkg_root}/${VCPKG_TARGET_TRIPLET}/lib")
            set(_quic_ssl_file "${_quic_vcpkg_libdir}/libssl.lib")
            set(_quic_crypto_file "${_quic_vcpkg_libdir}/libcrypto.lib")
        endif()
    else()
        set(_quic_ssl_file "${OPENSSL_SSL_LIBRARY}")
        set(_quic_crypto_file "${OPENSSL_CRYPTO_LIBRARY}")
    endif()
    if(WIN32 AND _quic_shared)
        # FindOpenSSL also caches legacy release/debug variables. Override
        # both configurations so a prior static configure cannot leak into
        # this shared external project.
        set(_quic_openssl_cache_args
            "-DSSL_EAY_RELEASE:FILEPATH=${_quic_ssl_file}"
            "-DLIB_EAY_RELEASE:FILEPATH=${_quic_crypto_file}"
            "-DSSL_EAY_DEBUG:FILEPATH=${_quic_ssl_file}"
            "-DLIB_EAY_DEBUG:FILEPATH=${_quic_crypto_file}")
    else()
        set(_quic_openssl_cache_args)
    endif()

    set(_ngtcp2_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ngtcp2")
    set(_nghttp3_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/nghttp3")
    # External CMake projects cannot share a binary directory across
    # generators. Keep each generator/toolset's build tree in the dependency
    # cache so Ninja, Visual Studio, and other valid host setups coexist.
    string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _quic_generator_cache "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _quic_platform_cache "${CMAKE_GENERATOR_PLATFORM}")
        string(APPEND _quic_generator_cache "-" "${_quic_platform_cache}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        string(REGEX REPLACE "[^A-Za-z0-9_.-]" "-" _quic_toolset_cache "${CMAKE_GENERATOR_TOOLSET}")
        string(APPEND _quic_generator_cache "-" "${_quic_toolset_cache}")
    endif()
    set(_ngtcp2_build "${ASCIICHAT_DEPS_CACHE_DIR}/asciichat-ngtcp2-${_quic_generator_cache}")
    set(_nghttp3_build "${ASCIICHAT_DEPS_CACHE_DIR}/asciichat-nghttp3-${_quic_generator_cache}")
    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            set(_quic_config_name Release)
        else()
            set(_quic_config_name Debug)
        endif()
        set(_quic_config_suffix "/${_quic_config_name}")
        set(_quic_config_args --config "${_quic_config_name}")
    else()
        set(_quic_config_suffix)
        set(_quic_config_args)
    endif()
    if(EXISTS "${_nghttp3_build}/CMakeCache.txt")
        file(READ "${_nghttp3_build}/CMakeCache.txt" _nghttp3_cache)
        string(FIND "${_nghttp3_cache}" "CMAKE_HOME_DIRECTORY:INTERNAL=${_nghttp3_source}" _nghttp3_source_match)
        if(_nghttp3_source_match EQUAL -1)
            file(REMOVE_RECURSE "${_nghttp3_build}")
        endif()
    endif()
    set(_ngtcp2_library "${_ngtcp2_build}/lib${_quic_config_suffix}/${_quic_library_prefix}ngtcp2${_quic_library_suffix}")
    set(_ngtcp2_crypto_library "${_ngtcp2_build}/crypto/ossl${_quic_config_suffix}/${_quic_library_prefix}ngtcp2_crypto_ossl${_quic_library_suffix}")
    set(_nghttp3_library "${_nghttp3_build}/lib${_quic_config_suffix}/${_quic_library_prefix}nghttp3${_quic_library_suffix}")
    if(WIN32 AND _quic_shared)
        set(_ngtcp2_import_library "${_ngtcp2_build}/lib${_quic_config_suffix}/ngtcp2.lib")
        set(_ngtcp2_crypto_import_library "${_ngtcp2_build}/crypto/ossl${_quic_config_suffix}/ngtcp2_crypto_ossl.lib")
        set(_nghttp3_import_library "${_nghttp3_build}/lib${_quic_config_suffix}/nghttp3.lib")
    endif()
    set(_ngtcp2_byproducts "${_ngtcp2_library}" "${_ngtcp2_crypto_library}")
    set(_nghttp3_byproducts "${_nghttp3_library}")
    if(WIN32 AND _quic_shared)
        list(APPEND _ngtcp2_byproducts "${_ngtcp2_import_library}" "${_ngtcp2_crypto_import_library}")
        list(APPEND _nghttp3_byproducts "${_nghttp3_import_library}")
    endif()
    file(MAKE_DIRECTORY "${_ngtcp2_build}/lib/includes" "${_nghttp3_build}/lib/includes")

    # Configure the isolated dependency projects while the parent project is
    # configuring.  ExternalProject_Add normally defers both configure and
    # build until the first build, which makes the first Ninja invocation look
    # as though it is unexpectedly configuring third-party projects.  Keep
    # compilation in the build graph; only run the inexpensive CMake configure
    # phase here so imported targets and generated headers have a predictable
    # build tree before generation finishes.
    set(_quic_generator_args -G "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND _quic_generator_args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND _quic_generator_args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_ngtcp2_source}" -B "${_ngtcp2_build}"
            ${_quic_generator_args} ${_quic_common_args}
            -DENABLE_OPENSSL:BOOL=ON
            -DOPENSSL_INCLUDE_DIR:PATH=${OPENSSL_INCLUDE_DIR}
            -DOPENSSL_INCLUDE_DIRS:PATH=${OPENSSL_INCLUDE_DIR}
            -DOPENSSL_ROOT_DIR:PATH=${_quic_vcpkg_root}/${VCPKG_TARGET_TRIPLET}
            -D_OPENSSL_INCLUDE_DIRS:INTERNAL=${OPENSSL_INCLUDE_DIR}
            -D_OPENSSL_INCLUDEDIR:INTERNAL=${OPENSSL_INCLUDE_DIR}
            -DOPENSSL_SSL_LIBRARY:FILEPATH=${_quic_ssl_file}
            -DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${_quic_crypto_file}
            ${_quic_openssl_cache_args}
            -DVCPKG_MANIFEST_INSTALL:BOOL=OFF
        RESULT_VARIABLE _ngtcp2_configure_result
        OUTPUT_VARIABLE _ngtcp2_configure_stdout
        ERROR_VARIABLE _ngtcp2_configure_stderr)
    if(NOT _ngtcp2_configure_result EQUAL 0)
        message(FATAL_ERROR "Failed to configure ngtcp2 (${_ngtcp2_configure_result})\n${_ngtcp2_configure_stdout}${_ngtcp2_configure_stderr}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${_nghttp3_source}" -B "${_nghttp3_build}"
            ${_quic_generator_args} ${_quic_common_args}
            -DVCPKG_MANIFEST_INSTALL:BOOL=OFF
        RESULT_VARIABLE _nghttp3_configure_result
        OUTPUT_VARIABLE _nghttp3_configure_stdout
        ERROR_VARIABLE _nghttp3_configure_stderr)
    if(NOT _nghttp3_configure_result EQUAL 0)
        message(FATAL_ERROR "Failed to configure nghttp3 (${_nghttp3_configure_result})\n${_nghttp3_configure_stdout}${_nghttp3_configure_stderr}")
    endif()

    if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        set(_quic_build_tail)
    else()
        set(_quic_build_tail -- --quiet)
    endif()

    ExternalProject_Add(asciichat-ngtcp2-build
        SOURCE_DIR "${_ngtcp2_source}"
        BINARY_DIR "${_ngtcp2_build}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E true INSTALL_COMMAND ""
        CMAKE_ARGS ${_quic_common_args} "-DENABLE_OPENSSL:BOOL=ON"
            "-DOPENSSL_INCLUDE_DIR:PATH=${OPENSSL_INCLUDE_DIR}"
            "-DOPENSSL_INCLUDE_DIRS:PATH=${OPENSSL_INCLUDE_DIR}"
            "-DOPENSSL_ROOT_DIR:PATH=${_quic_vcpkg_root}/${VCPKG_TARGET_TRIPLET}"
            "-D_OPENSSL_INCLUDE_DIRS:INTERNAL=${OPENSSL_INCLUDE_DIR}"
            "-D_OPENSSL_INCLUDEDIR:INTERNAL=${OPENSSL_INCLUDE_DIR}"
            "-DOPENSSL_SSL_LIBRARY:FILEPATH=${_quic_ssl_file}"
            "-DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${_quic_crypto_file}"
            ${_quic_openssl_cache_args}
            "-DVCPKG_MANIFEST_INSTALL:BOOL=OFF"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> ${_quic_config_args} --target ${_quic_ngtcp2_crypto_target} ${_quic_build_tail}
        BUILD_BYPRODUCTS ${_ngtcp2_byproducts}
        EXCLUDE_FROM_ALL TRUE)

    ExternalProject_Add(asciichat-nghttp3-build
        SOURCE_DIR "${_nghttp3_source}"
        BINARY_DIR "${_nghttp3_build}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E true INSTALL_COMMAND ""
        CMAKE_ARGS ${_quic_common_args} "-DVCPKG_MANIFEST_INSTALL:BOOL=OFF"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> ${_quic_config_args} --target ${_quic_nghttp3_target} ${_quic_build_tail}
        BUILD_BYPRODUCTS ${_nghttp3_byproducts}
        EXCLUDE_FROM_ALL TRUE)

    add_library(asciichat-ngtcp2 ${_quic_shared_type} IMPORTED GLOBAL)
    set_target_properties(asciichat-ngtcp2 PROPERTIES IMPORTED_LOCATION "${_ngtcp2_library}")
    if(WIN32 AND _quic_shared)
        set_target_properties(asciichat-ngtcp2 PROPERTIES IMPORTED_IMPLIB "${_ngtcp2_import_library}")
    endif()
    add_library(asciichat-ngtcp2-crypto ${_quic_shared_type} IMPORTED GLOBAL)
    set_target_properties(asciichat-ngtcp2-crypto PROPERTIES IMPORTED_LOCATION "${_ngtcp2_crypto_library}")
    if(WIN32 AND _quic_shared)
        set_target_properties(asciichat-ngtcp2-crypto PROPERTIES IMPORTED_IMPLIB "${_ngtcp2_crypto_import_library}")
    endif()
    add_library(asciichat-nghttp3 ${_quic_shared_type} IMPORTED GLOBAL)
    set_target_properties(asciichat-nghttp3 PROPERTIES IMPORTED_LOCATION "${_nghttp3_library}")
    if(WIN32 AND _quic_shared)
        set_target_properties(asciichat-nghttp3 PROPERTIES IMPORTED_IMPLIB "${_nghttp3_import_library}")
    endif()

    add_library(asciichat-quic INTERFACE)
    add_dependencies(asciichat-quic asciichat-ngtcp2-build asciichat-nghttp3-build)
    target_include_directories(asciichat-quic INTERFACE
        "${_ngtcp2_source}/lib/includes" "${_ngtcp2_build}/lib/includes" "${_ngtcp2_source}/crypto/includes"
        "${_nghttp3_source}/lib/includes" "${_nghttp3_build}/lib/includes")
    if(NOT _quic_shared)
        target_compile_definitions(asciichat-quic INTERFACE NGTCP2_STATICLIB NGHTTP3_STATICLIB)
    endif()
    target_link_libraries(asciichat-quic INTERFACE asciichat-ngtcp2-crypto asciichat-ngtcp2 asciichat-nghttp3 OpenSSL::SSL)
    if(WIN32)
        target_link_libraries(asciichat-quic INTERFACE ws2_32 bcrypt crypt32)
    endif()
endfunction()
