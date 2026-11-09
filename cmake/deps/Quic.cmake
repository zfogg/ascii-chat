# ngtcp2's OpenSSL QUIC TLS backend requires the mainline QUIC TLS API. Probe
# the selected headers and linked SSL library, not the version of an openssl
# executable that might come from a different installation.
include(CheckCSourceCompiles)
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
    # Compile and link: a static-library try-compile would not detect a header
    # and library from different OpenSSL installations.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    unset(ASCIICHAT_OPENSSL_QUIC_TLS_LINKS CACHE)
    check_c_source_compiles("\
#include <openssl/ssl.h>
#if OPENSSL_VERSION_NUMBER < 0x30600000L
#error ascii-chat requires OpenSSL 3.6.0 or newer
#endif
int main(void) { return SSL_set_quic_tls_cbs((SSL *)0, (const OSSL_DISPATCH *)0, 0); }
" ASCIICHAT_OPENSSL_QUIC_TLS_LINKS)
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

    set(_quic_common_args
        "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
        "-DENABLE_LIB_ONLY:BOOL=ON"
        "-DENABLE_STATIC_LIB:BOOL=ON"
        "-DENABLE_SHARED_LIB:BOOL=OFF"
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

    get_target_property(_quic_ssl_type OpenSSL::SSL TYPE)
    get_target_property(_quic_crypto_type OpenSSL::Crypto TYPE)
    if(_quic_ssl_type STREQUAL "SHARED_LIBRARY")
        set(_quic_ssl_file "$<TARGET_LINKER_FILE:OpenSSL::SSL>")
    else()
        set(_quic_ssl_file "$<TARGET_FILE:OpenSSL::SSL>")
    endif()
    if(_quic_crypto_type STREQUAL "SHARED_LIBRARY")
        set(_quic_crypto_file "$<TARGET_LINKER_FILE:OpenSSL::Crypto>")
    else()
        set(_quic_crypto_file "$<TARGET_FILE:OpenSSL::Crypto>")
    endif()

    set(_ngtcp2_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/ngtcp2")
    set(_nghttp3_source "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/nghttp3")
    set(_ngtcp2_build "${CMAKE_BINARY_DIR}/_deps/asciichat-ngtcp2")
    set(_nghttp3_build "${CMAKE_BINARY_DIR}/_deps/asciichat-nghttp3")
    set(_ngtcp2_library "${_ngtcp2_build}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ngtcp2${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(_ngtcp2_crypto_library "${_ngtcp2_build}/crypto/ossl/${CMAKE_STATIC_LIBRARY_PREFIX}ngtcp2_crypto_ossl${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(_nghttp3_library "${_nghttp3_build}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}nghttp3${CMAKE_STATIC_LIBRARY_SUFFIX}")
    file(MAKE_DIRECTORY "${_ngtcp2_build}/lib/includes" "${_nghttp3_build}/lib/includes")

    ExternalProject_Add(asciichat-ngtcp2-build
        SOURCE_DIR "${_ngtcp2_source}"
        BINARY_DIR "${_ngtcp2_build}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND ""
        CMAKE_ARGS ${_quic_common_args} "-DENABLE_OPENSSL:BOOL=ON"
            "-DOPENSSL_INCLUDE_DIR:PATH=${OPENSSL_INCLUDE_DIR}"
            "-DOPENSSL_SSL_LIBRARY:FILEPATH=${_quic_ssl_file}"
            "-DOPENSSL_CRYPTO_LIBRARY:FILEPATH=${_quic_crypto_file}"
            "-DVCPKG_MANIFEST_INSTALL:BOOL=OFF"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ngtcp2_crypto_ossl_static
        BUILD_BYPRODUCTS "${_ngtcp2_library}" "${_ngtcp2_crypto_library}"
        BUILD_ALWAYS TRUE
        EXCLUDE_FROM_ALL TRUE)

    ExternalProject_Add(asciichat-nghttp3-build
        SOURCE_DIR "${_nghttp3_source}"
        BINARY_DIR "${_nghttp3_build}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND "" INSTALL_COMMAND ""
        CMAKE_ARGS ${_quic_common_args} "-DVCPKG_MANIFEST_INSTALL:BOOL=OFF"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target nghttp3_static
        BUILD_BYPRODUCTS "${_nghttp3_library}"
        BUILD_ALWAYS TRUE
        EXCLUDE_FROM_ALL TRUE)

    add_library(asciichat-ngtcp2 STATIC IMPORTED GLOBAL)
    set_target_properties(asciichat-ngtcp2 PROPERTIES IMPORTED_LOCATION "${_ngtcp2_library}")
    add_library(asciichat-ngtcp2-crypto STATIC IMPORTED GLOBAL)
    set_target_properties(asciichat-ngtcp2-crypto PROPERTIES IMPORTED_LOCATION "${_ngtcp2_crypto_library}")
    add_library(asciichat-nghttp3 STATIC IMPORTED GLOBAL)
    set_target_properties(asciichat-nghttp3 PROPERTIES IMPORTED_LOCATION "${_nghttp3_library}")

    add_library(asciichat-quic INTERFACE)
    add_dependencies(asciichat-quic asciichat-ngtcp2-build asciichat-nghttp3-build)
    target_include_directories(asciichat-quic INTERFACE
        "${_ngtcp2_source}/lib/includes" "${_ngtcp2_build}/lib/includes" "${_ngtcp2_source}/crypto/includes"
        "${_nghttp3_source}/lib/includes" "${_nghttp3_build}/lib/includes")
    target_compile_definitions(asciichat-quic INTERFACE NGTCP2_STATICLIB NGHTTP3_STATICLIB)
    target_link_libraries(asciichat-quic INTERFACE asciichat-ngtcp2-crypto asciichat-ngtcp2 asciichat-nghttp3 OpenSSL::SSL)
    if(WIN32)
        target_link_libraries(asciichat-quic INTERFACE ws2_32 bcrypt crypt32)
    endif()
endfunction()
