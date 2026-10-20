cmake_minimum_required(VERSION 3.30)

if(NOT DEFINED EXECUTABLE OR NOT EXPECTED_VERSION MATCHES "^v[0-9]+[.][0-9]+[.][0-9]+$")
    message(FATAL_ERROR "Pass EXECUTABLE and EXPECTED_VERSION=vMAJOR.MINOR.PATCH")
endif()
execute_process(COMMAND "${EXECUTABLE}" --version
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 15)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Release --version failed (${result}): ${output}\n${error}")
endif()
string(REGEX MATCH "^ascii-chat ([^ \r\n]+) " matched "${output}")
if(NOT matched OR NOT CMAKE_MATCH_1 STREQUAL EXPECTED_VERSION)
    message(FATAL_ERROR "Expected release ${EXPECTED_VERSION}, got: ${output}\n${error}")
endif()
message(STATUS "Verified release: ${output}")
