set(_file "${LWS_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# zlib is built later in the same graph. TLS/platform probes only need their own libraries.
string(REPLACE "set(CMAKE_REQUIRED_LIBRARIES \${CMAKE_REQUIRED_LIBRARIES} \${ZLIB_LIBRARIES})"
    "" _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()

# OpenSSL 1.1+ makes these key contexts opaque.  The pinned libwebsockets
# compatibility probes can fail during a musl cross-compile, so select the
# modern API paths from the OpenSSL version as well.
foreach(_relative include/libwebsockets/lws-genhash.h lib/tls/openssl/lws-genec.c lib/tls/openssl/lws-genrsa.c)
    set(_file "${LWS_SOURCE_DIR}/${_relative}")
    file(READ "${_file}" _contents)
    if(NOT _contents MATCHES "asciichat OpenSSL opaque")
        string(REPLACE "#else\n\tconst EVP_MD *evp_type;"
            "#else\n/* asciichat OpenSSL opaque HMAC */\n#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L\n#define LWS_HAVE_HMAC_CTX_new 1\n#endif\n\tconst EVP_MD *evp_type;"
            _contents "${_contents}")
        string(REGEX REPLACE "(#include \"private-lib-tls-openssl.h\")"
            "\\1\n/* asciichat OpenSSL opaque key */\n#if defined(OPENSSL_VERSION_NUMBER) && OPENSSL_VERSION_NUMBER >= 0x10100000L\n#define LWS_HAVE_ECDSA_SIG_set0 1\n#define LWS_HAVE_RSA_SET0_KEY 1\n#endif"
            _contents "${_contents}")
        file(WRITE "${_file}" "${_contents}")
    endif()
endforeach()
foreach(_relative CMakeLists.txt lib/CMakeLists.txt lib/tls/CMakeLists.txt)
    set(_file "${LWS_SOURCE_DIR}/${_relative}")
    file(READ "${_file}" _contents)
    if(NOT _contents MATCHES "asciichat target probes")
        string(REGEX REPLACE "(set\\(CMAKE_REQUIRED_LIBRARIES[^\n]*\\))"
            "\\1\n# asciichat target probes exclude the not-yet-built compression library.\nlist(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES \${ZLIB_LIBRARIES} asciichat-zlib ZLIB::ZLIB)"
            _contents "${_contents}")
        file(WRITE "${_file}" "${_contents}")
    endif()
endforeach()

set(_file "${LWS_SOURCE_DIR}/lib/CMakeLists.txt")
file(READ "${_file}" _contents)
set(_original "${_contents}")
# Staged TLS headers belong to the build tree, not an installed package interface.
string(REPLACE "\"$<BUILD_INTERFACE:\${inc}>\" \"$<INSTALL_INTERFACE:\${inc}>\""
    "\"$<BUILD_INTERFACE:\${inc}>\"" _contents "${_contents}")
if(NOT _contents STREQUAL _original)
    file(WRITE "${_file}" "${_contents}")
endif()
