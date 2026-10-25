cmake_minimum_required(VERSION 3.20)

foreach(required CC AR RANLIB STATIC_ARCHIVE_TEST_BINARY_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
endfunction()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef case_id)
set(work "${STATIC_ARCHIVE_TEST_BINARY_DIR}/${case_id}")
set(objects "${work}/CMakeFiles/ascii-chat-core.dir/lib")
file(MAKE_DIRECTORY "${objects}")
foreach(name stable retired replacement)
    file(WRITE "${work}/${name}.c" "int ${name}(void) { return 7; }\n")
    run_checked("${CC}" -c "${work}/${name}.c" -o "${objects}/${name}.c.o")
endforeach()

set(archive "${work}/libfixture.a")
set(object_list "${work}/objects.txt")
get_filename_component(repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(WRITE "${object_list}" "${objects}/stable.c.o;${objects}/retired.c.o")
run_checked("${CMAKE_COMMAND}" "-DAR=${AR}" "-DRANLIB=${RANLIB}" "-DOUTPUT=${archive}"
    "-DOBJECT_LIST_FILE=${object_list}" "-DCMAKE_CURRENT_BINARY_DIR=${work}"
    -P "${repo}/cmake/scripts/create_static_archive.cmake")

# A second build removes one source and adds another while retaining a stable object.
file(WRITE "${object_list}" "${objects}/stable.c.o;${objects}/replacement.c.o")
run_checked("${CMAKE_COMMAND}" "-DAR=${AR}" "-DRANLIB=${RANLIB}" "-DOUTPUT=${archive}"
    "-DOBJECT_LIST_FILE=${object_list}" "-DCMAKE_CURRENT_BINARY_DIR=${work}"
    -P "${repo}/cmake/scripts/create_static_archive.cmake")
execute_process(COMMAND "${AR}" t "${archive}" RESULT_VARIABLE result OUTPUT_VARIABLE members ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect archive: ${error}")
endif()
string(REPLACE "\r\n" "\n" members "${members}")
string(STRIP "${members}" members)
string(REPLACE "\n" ";" members "${members}")
list(SORT members)
if(NOT members STREQUAL "lib_replacement.c.o;lib_stable.c.o")
    message(FATAL_ERROR "Archive retains stale objects or lost current objects: ${members}")
endif()
message(STATUS "Rebuilt archive contains exactly the current object list")
