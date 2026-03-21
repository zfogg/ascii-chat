# Build libvterm without libtool (Ubuntu 24.04 doesn't provide /usr/bin/libtool)
# Compiles all .c files directly and creates a static archive.

# Generate encoding .inc files from .tbl files
file(GLOB TBL_FILES "${SOURCE_DIR}/src/encoding/*.tbl")
foreach(tbl ${TBL_FILES})
    get_filename_component(tbl_name ${tbl} NAME_WE)
    set(inc_file "${SOURCE_DIR}/src/encoding/${tbl_name}.inc")
    if(NOT EXISTS "${inc_file}")
        execute_process(
            COMMAND perl -CSD "${SOURCE_DIR}/tbl2inc_c.pl" "${tbl}"
            OUTPUT_FILE "${inc_file}"
            RESULT_VARIABLE TBL_RESULT
        )
        if(NOT TBL_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to generate ${inc_file} from ${tbl}")
        endif()
    endif()
endforeach()

# Compile all source files
file(GLOB C_FILES "${SOURCE_DIR}/src/*.c")
set(OBJ_FILES "")
foreach(src ${C_FILES})
    get_filename_component(name ${src} NAME_WE)
    set(obj "${SOURCE_DIR}/${name}.o")
    execute_process(
        COMMAND ${MUSL_GCC} -O2 -fPIC -Wno-error
            -isystem ${KERNEL_HEADERS_DIR}
            -I${SOURCE_DIR}/include -std=c99
            -c ${src} -o ${obj}
        RESULT_VARIABLE CC_RESULT
    )
    if(NOT CC_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to compile ${src}")
    endif()
    list(APPEND OBJ_FILES ${obj})
endforeach()

# Create static archive
execute_process(
    COMMAND ar rcs "${SOURCE_DIR}/libvterm.a" ${OBJ_FILES}
    RESULT_VARIABLE AR_RESULT
)
if(NOT AR_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to create libvterm.a")
endif()

message(STATUS "Built libvterm.a successfully")
