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
        set(_quic_shared ${ASCIICHAT_SHARED_DEPS})
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
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
        "-DENABLE_LIB_ONLY:BOOL=ON"
        "-DENABLE_STATIC_LIB:BOOL=${_quic_enable_static}"
        "-DENABLE_SHARED_LIB:BOOL=${_quic_enable_shared}"
        "-DBUILD_TESTING:BOOL=OFF")
    foreach(_variable CMAKE_TOOLCHAIN_FILE CMAKE_SYSROOT CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES
                      CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_C_COMPILER_TARGET CMAKE_MSVC_RUNTIME_LIBRARY
                      CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
                      CMAKE_C_FLAGS_MINSIZEREL VCPKG_INSTALLED_DIR VCPKG_TARGET_TRIPLET)
        if(DEFINED ${_variable} AND NOT "${${_variable}}" STREQUAL "")
            list(APPEND _quic_common_args "-D${_variable}:STRING=${${_variable}}")
        endif()
    endforeach()
    if(USE_MUSL)
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
    set(_quic_ssl_file "${OPENSSL_SSL_LIBRARY}")
    set(_quic_crypto_file "${OPENSSL_CRYPTO_LIBRARY}")

    set(_ngtcp2_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ngtcp2")
    set(_nghttp3_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/nghttp3")
    set(_ngtcp2_build "${CMAKE_BINARY_DIR}/_deps/asciichat-ngtcp2")
    set(_nghttp3_build "${CMAKE_BINARY_DIR}/_deps/asciichat-nghttp3")
    set(_ngtcp2_library "${_ngtcp2_build}/lib/${_quic_library_prefix}ngtcp2${_quic_library_suffix}")
    set(_ngtcp2_crypto_library "${_ngtcp2_build}/crypto/ossl/${_quic_library_prefix}ngtcp2_crypto_ossl${_quic_library_suffix}")
    set(_nghttp3_library "${_nghttp3_build}/lib/${_quic_library_prefix}nghttp3${_quic_library_suffix}")
    if(WIN32 AND _quic_shared)
        set(_ngtcp2_import_library "${_ngtcp2_build}/lib/ngtcp2.lib")
        set(_ngtcp2_crypto_import_library "${_ngtcp2_build}/crypto/ossl/ngtcp2_crypto_ossl.lib")
        set(_nghttp3_import_library "${_nghttp3_build}/lib/nghttp3.lib")
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
            -DOPENSSL_SSL_LIBRARY:FILEPATH=${_quic_ssl_file}
            -DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${_quic_crypto_file}
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

    ExternalProject_Add(asciichat-ngtcp2-build
        SOURCE_DIR "${_ngtcp2_source}"
        BINARY_DIR "${_ngtcp2_build}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E true INSTALL_COMMAND ""
        CMAKE_ARGS ${_quic_common_args} "-DENABLE_OPENSSL:BOOL=ON"
            "-DOPENSSL_INCLUDE_DIR:PATH=${OPENSSL_INCLUDE_DIR}"
            "-DOPENSSL_SSL_LIBRARY:FILEPATH=${_quic_ssl_file}"
            "-DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${_quic_crypto_file}"
            "-DVCPKG_MANIFEST_INSTALL:BOOL=OFF"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ${_quic_ngtcp2_crypto_target} -- --quiet
        BUILD_BYPRODUCTS "${_ngtcp2_library}" "${_ngtcp2_crypto_library}"
        BUILD_ALWAYS TRUE
        EXCLUDE_FROM_ALL TRUE)

    ExternalProject_Add(asciichat-nghttp3-build
        SOURCE_DIR "${_nghttp3_source}"
        BINARY_DIR "${_nghttp3_build}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E true INSTALL_COMMAND ""
        CMAKE_ARGS ${_quic_common_args} "-DVCPKG_MANIFEST_INSTALL:BOOL=OFF"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ${_quic_nghttp3_target} -- --quiet
        BUILD_BYPRODUCTS "${_nghttp3_library}"
        BUILD_ALWAYS TRUE
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
