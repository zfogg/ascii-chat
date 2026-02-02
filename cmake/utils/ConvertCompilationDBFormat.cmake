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

# Read the compilation database
file(READ "${INPUT_FILE}" _db_content)

# Try to use Python to convert the format (preferred method)
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
            except Exception as e:
                print(f'Warning: Failed to parse command in entry: {e}')

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
        ERROR_VARIABLE _conv_error
    )

    file(REMOVE "${_conv_script}")

    if(NOT _conv_result EQUAL 0)
        message(STATUS "Python conversion attempt failed (will use CMake fallback): ${_conv_error}")
        set(_use_cmake_fallback TRUE)
    else()
        message(STATUS "Python: Converted compilation database format: ${_conv_output}")
    endif()
else()
    message(STATUS "Python3 not found during build - using CMake-based fallback conversion")
    set(_use_cmake_fallback TRUE)
endif()

# CMake fallback: convert compilation database without Python
if(_use_cmake_fallback)
    message(STATUS "Converting compilation database format (CMake fallback)...")

    # Read the JSON file
    file(READ "${INPUT_FILE}" _json_content)

    # Simple regex-based conversion: find entries with "command" but no "arguments"
    # Pattern: "command": "..."
    # Replace with: "command": "...", "arguments": [...]

    # This is a simplified conversion that works for compilation databases
    # by splitting the command string on spaces (with basic quote handling)

    string(REGEX MATCHALL "\"command\": \"[^\"]*\"" _commands "${_json_content}")
    set(_converted_content "${_json_content}")

    foreach(_cmd_entry ${_commands})
        # Extract the command string value (between the quotes)
        string(REGEX MATCH "\"command\": \"(.*)\"" _match "${_cmd_entry}")
        if(CMAKE_MATCH_1)
            set(_cmd_value "${CMAKE_MATCH_1}")

            # Escape backslashes and quotes for JSON
            string(REPLACE "\\" "\\\\" _cmd_value "${_cmd_value}")
            string(REPLACE "\"" "\\\"" _cmd_value "${_cmd_value}")

            # Create arguments array by splitting on spaces (basic approach)
            # For more robust parsing, this would need shell-aware splitting
            string(REGEX MATCHALL "[^ ]+" _args "${CMAKE_MATCH_1}")

            # Build the arguments JSON array
            set(_args_json "[")
            set(_first TRUE)
            foreach(_arg ${_args})
                if(NOT _first)
                    string(APPEND _args_json ", ")
                endif()
                set(_first FALSE)

                # Escape the argument for JSON
                string(REPLACE "\\" "\\\\" _arg "${_arg}")
                string(REPLACE "\"" "\\\"" _arg "${_arg}")

                string(APPEND _args_json "\"${_arg}\"")
            endforeach()
            string(APPEND _args_json "]")

            # Replace the original entry with one that includes arguments
            string(REPLACE "\"command\": \"${CMAKE_MATCH_1}\""
                          "\"command\": \"${_cmd_value}\", \"arguments\": ${_args_json}"
                          _converted_content
                          "${_converted_content}")
        endif()
    endforeach()

    # Write back the modified compilation database
    file(WRITE "${INPUT_FILE}" "${_converted_content}")
    message(STATUS "CMake fallback conversion complete")
endif()

# Verify the conversion worked
file(READ "${INPUT_FILE}" _final_content)
if(_final_content MATCHES "\"arguments\"")
    message(STATUS "✓ Compilation database now includes 'arguments' arrays for LibTooling")
else()
    message(WARNING "⚠ Compilation database conversion may have failed - no 'arguments' field found")
endif()
