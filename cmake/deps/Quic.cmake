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
    set(CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL ${CMAKE_DL_LIBS})
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

asciichat_check_quic_tls()
