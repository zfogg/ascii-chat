# =============================================================================
# Convert Compilation Database Format
# =============================================================================
# Post-processes compile_commands.json to convert from "command" format
# (single command string) to "arguments" format (array of arguments).
#
# LibTooling and some source transformation tools require the arguments
# to be in an array format for proper parsing and include path resolution.

if(NOT INPUT_FILE)
    message(FATAL_ERROR "ConvertCompilationDBFormat: INPUT_FILE not specified")
endif()

# Try to use Python to convert the format
find_package(Python3 QUIET)

if(Python3_FOUND)
    # Create a temporary Python script to do the conversion
    set(_conv_script "${CMAKE_BINARY_DIR}/.tmp_compile_db_conv.py")
    file(WRITE "${_conv_script}" "
import json
import sys
import shlex

def convert_db(path):
    with open(path, 'r') as f:
        db = json.load(f)

    count = 0
    for entry in db:
        if 'command' in entry and 'arguments' not in entry:
            try:
                entry['arguments'] = shlex.split(entry['command'])
                count += 1
            except:
                pass

    with open(path, 'w') as f:
        json.dump(db, f, indent=2)

    return count

if __name__ == '__main__':
    count = convert_db(sys.argv[1])
    print(f'Converted {count} entries')
")

    # Run the Python script to convert the format
    execute_process(
        COMMAND ${Python3_EXECUTABLE} "${_conv_script}" "${INPUT_FILE}"
        RESULT_VARIABLE _conv_result
        OUTPUT_VARIABLE _conv_output
    )

    file(REMOVE "${_conv_script}")

    if(NOT _conv_result EQUAL 0)
        message(WARNING "Failed to convert compilation database format: ${_conv_output}")
    else()
        message(STATUS "Converted compilation database format: ${_conv_output}")
    endif()
else()
    message(STATUS "Python3 not found - skipping compilation database conversion to 'arguments' format")
    message(STATUS "The defer tool will use the 'command' field instead, which may work with some tools")
endif()
