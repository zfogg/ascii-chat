# bin2c.cmake: converts INPUT binary file to OUTPUT C source with VAR_NAME array.
# Usage: cmake -DINPUT=file.ttf -DOUTPUT=file.c -DVAR_NAME=g_my_font -P bin2c.cmake

file(READ "${INPUT}" _content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _content "${_content}")
file(WRITE "${OUTPUT}"
    "#include <stddef.h>\n"
    "const unsigned char ${VAR_NAME}[] = {\n${_content}\n};\n"
    "const size_t ${VAR_NAME}_size = sizeof(${VAR_NAME});\n"
)
message(STATUS "Generated ${OUTPUT} from ${INPUT}")
