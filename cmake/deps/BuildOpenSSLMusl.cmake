cmake_minimum_required(VERSION 3.20)

foreach(_required IN ITEMS OPENSSL_SOURCE_DIR OPENSSL_TARGET OPENSSL_PREFIX
    OPENSSL_CC OPENSSL_CFLAGS OPENSSL_JOBS OPENSSL_CONFIG_STAMP OPENSSL_SIGNATURE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required to build OpenSSL for musl")
    endif()
endforeach()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
        "CC=${OPENSSL_CC}" "CFLAGS=${OPENSSL_CFLAGS}"
        "${OPENSSL_SOURCE_DIR}/Configure" "${OPENSSL_TARGET}"
        "--prefix=${OPENSSL_PREFIX}" --libdir=lib
        no-shared no-module no-apps enable-quic no-zlib no-async
        no-tests no-ui-console -fPIC
    WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
    RESULT_VARIABLE _configure_result
    OUTPUT_VARIABLE _configure_output
    ERROR_VARIABLE _configure_error)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "Failed to configure OpenSSL:\n${_configure_output}\n${_configure_error}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env make -j${OPENSSL_JOBS}
    WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_error)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "Failed to build OpenSSL:\n${_build_output}\n${_build_error}")
endif()

execute_process(
    COMMAND make install_sw
    WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
    RESULT_VARIABLE _install_result
    OUTPUT_VARIABLE _install_output
    ERROR_VARIABLE _install_error)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "Failed to install OpenSSL:\n${_install_output}\n${_install_error}")
endif()

file(WRITE "${OPENSSL_CONFIG_STAMP}" "${OPENSSL_SIGNATURE}\n")
