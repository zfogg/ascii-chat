# =============================================================================
# Timer Script
# =============================================================================
# Records build start/end time and prints colored timing message
#
# Usage:
#   cmake -DACTION=start -DTARGET_NAME=<name> -P Timer.cmake
#   cmake -DACTION=end -DTARGET_NAME=<name> -P Timer.cmake
#
# Requires:
#   - ACTION: "start" or "end"
#   - TARGET_NAME: Name of the target being built
# =============================================================================

# Import color definitions from Colors.cmake
include(${SOURCE_DIR}/cmake/utils/Colors.cmake)

# Time file path
set(TIME_FILE "${CMAKE_BINARY_DIR}/.build_time_${TARGET_NAME}.txt")

# =============================================================================
# Function: get_timestamp_ms
# Gets current timestamp in milliseconds since epoch
# Uses python3 for actual millisecond precision on Unix
# Falls back to CMake-only approach (second precision) if python3 unavailable
# Sets the output variable to the timestamp value
# =============================================================================
function(get_timestamp_ms OUTPUT_VAR)
    if(WIN32)
        # Windows: Try bash first (Git Bash or WSL) for millisecond precision
        execute_process(
            COMMAND bash -c "date +%s%3N"
            OUTPUT_VARIABLE TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE BASH_RESULT
        )
        if(NOT BASH_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Fallback to python
            execute_process(
                COMMAND python -c "import time; print(int(time.time() * 1000))"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PYTHON_RESULT
            )
            if(NOT PYTHON_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
                # Fallback to PowerShell
                execute_process(
                    COMMAND powershell -NoProfile -Command "[int64]([datetime]::UtcNow - [datetime]'1970-01-01').TotalMilliseconds"
                    OUTPUT_VARIABLE TIMESTAMP
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE PS_RESULT
                )
                if(NOT PS_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
                    # Final fallback to CMake-only (second precision)
                    string(TIMESTAMP EPOCH_SEC "%s" UTC)
                    math(EXPR TIMESTAMP "${EPOCH_SEC} * 1000")
                endif()
            endif()
        endif()
    else()
        set(TIMESTAMP_OK FALSE)
        if(APPLE)
            # macOS: Try gdate first (GNU date from Homebrew coreutils)
            execute_process(
                COMMAND gdate +%s%3N
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE GDATE_RESULT
            )
            if(GDATE_RESULT EQUAL 0 AND NOT TIMESTAMP STREQUAL "")
                set(TIMESTAMP_OK TRUE)
            else()
                # Fallback to ruby (pre-installed on macOS)
                execute_process(
                    COMMAND ruby -e "puts (Time.now.to_f * 1000).to_i"
                    OUTPUT_VARIABLE TIMESTAMP
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE RUBY_RESULT
                )
                if(RUBY_RESULT EQUAL 0 AND NOT TIMESTAMP STREQUAL "")
                    set(TIMESTAMP_OK TRUE)
                endif()
            endif()
        elseif(PLATFORM_LINUX)
            # Linux: Try GNU date first for millisecond precision
            execute_process(
                COMMAND date +%s%3N
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE DATE_RESULT
            )
            if(DATE_RESULT EQUAL 0 AND NOT TIMESTAMP STREQUAL "")
                set(TIMESTAMP_OK TRUE)
            else()
                # Fallback to python3
                execute_process(
                    COMMAND python3 -c "import time; print(int(time.time() * 1000))"
                    OUTPUT_VARIABLE TIMESTAMP
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE PYTHON_RESULT
                )
                if(PYTHON_RESULT EQUAL 0 AND NOT TIMESTAMP STREQUAL "")
                    set(TIMESTAMP_OK TRUE)
                endif()
            endif()
        else()
            # Other Unix (BSDs, etc.): Try python3 first (BSD date doesn't support %N)
            execute_process(
                COMMAND python3 -c "import time; print(int(time.time() * 1000))"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PYTHON_RESULT
            )
            if(PYTHON_RESULT EQUAL 0 AND NOT TIMESTAMP STREQUAL "")
                set(TIMESTAMP_OK TRUE)
            endif()
        endif()
        if(NOT TIMESTAMP_OK)
            # Final fallback to CMake-only (second precision)
            string(TIMESTAMP EPOCH_SEC "%s" UTC)
            math(EXPR TIMESTAMP "${EPOCH_SEC} * 1000")
        endif()
    endif()

    set(${OUTPUT_VAR} "${TIMESTAMP}" PARENT_SCOPE)
endfunction()

if(ACTION STREQUAL "start")
    # Clear the total build time printed marker at the start of each build
    if(TARGET_NAME STREQUAL "build-total")
        set(TOTAL_MARKER_FILE "${CMAKE_BINARY_DIR}/.build_total_printed.txt")
        file(REMOVE "${TOTAL_MARKER_FILE}")
    endif()

    # Record start time with millisecond precision
    get_timestamp_ms(START_TIME)
    file(WRITE "${TIME_FILE}" "${START_TIME}")
elseif(ACTION STREQUAL "check")
    # Show success message only if target was NOT just rebuilt (POST_BUILD didn't run)
    # Check if the marker file exists and is fresh (created within last 2 seconds)
    set(MARKER_FILE "${CMAKE_BINARY_DIR}/.build_done_${TARGET_NAME}.txt")
    set(SHOW_MESSAGE TRUE)

    if(EXISTS "${MARKER_FILE}")
        file(READ "${MARKER_FILE}" MARKER_TIME)

        # Get current time in milliseconds
        get_timestamp_ms(CURRENT_TIME)

        # Calculate time difference
        math(EXPR TIME_DIFF_MS "${CURRENT_TIME} - ${MARKER_TIME}")

        # If marker is fresh (< 2000 milliseconds old), POST_BUILD just ran, skip message
        if(TIME_DIFF_MS LESS 2000)
            set(SHOW_MESSAGE FALSE)
        endif()
    endif()

    if(SHOW_MESSAGE)
        # Target was up-to-date, show success message with "(up to date)" in green
        set(MSG "${BoldGreen}✓${ColorReset} Built ${BoldCyan}${TARGET_NAME}${ColorReset} ${BoldGreen}(up to date)${ColorReset}")

        if(WIN32)
            # Use cmd.exe echo (fast) - Windows 10+ supports ANSI codes
            execute_process(COMMAND cmd /c "echo ${MSG}")
        else()
            execute_process(COMMAND printf "%b\n" "${MSG}")
        endif()
    endif()
elseif(ACTION STREQUAL "end")
    # For build-total, use a marker to ensure it only prints once
    # The marker is cleared at the start of each build by build-timer-start
    if(TARGET_NAME STREQUAL "build-total")
        set(TOTAL_MARKER_FILE "${CMAKE_BINARY_DIR}/.build_total_printed.txt")

        # Check if we already printed total time in this build
        if(EXISTS "${TOTAL_MARKER_FILE}")
            # Already printed, skip
            return()
        endif()

        # Mark that we're about to print total build time
        file(WRITE "${TOTAL_MARKER_FILE}" "1")
    endif()

    # Calculate elapsed time with millisecond precision
    if(EXISTS "${TIME_FILE}")
        file(READ "${TIME_FILE}" START_TIME)

        # Get end time with millisecond precision
        get_timestamp_ms(END_TIME)

        # Calculate elapsed in milliseconds
        math(EXPR ELAPSED_MS "${END_TIME} - ${START_TIME}")

        # Convert to seconds and milliseconds
        math(EXPR ELAPSED_SEC "${ELAPSED_MS} / 1000")
        math(EXPR MS_ONLY "${ELAPSED_MS} % 1000")

        # Zero-pad milliseconds to 3 digits (e.g., 56 -> 056, 5 -> 005)
        if(MS_ONLY LESS 10)
            set(MS_PADDED "00${MS_ONLY}")
        elseif(MS_ONLY LESS 100)
            set(MS_PADDED "0${MS_ONLY}")
        else()
            set(MS_PADDED "${MS_ONLY}")
        endif()

        # Format elapsed time
        if(ELAPSED_SEC LESS 60)
            if(ELAPSED_SEC LESS 7)
                set(TIME_STR "${BoldGreen}${ELAPSED_SEC}.${MS_PADDED}s${ColorReset}")
            else()
                set(TIME_STR "${BoldYellow}${ELAPSED_SEC}.${MS_PADDED}s${ColorReset}")
            endif()
        else()
            math(EXPR MINUTES "${ELAPSED_SEC} / 60")
            math(EXPR SECONDS "${ELAPSED_SEC} % 60")
            set(TIME_STR "${BoldMagenta}${MINUTES}m ${SECONDS}.${MS_PADDED}s${ColorReset}")
        endif()

        # Print success message with colored timing
        # Format message string with ANSI codes
        # Special formatting for build-total target
        if(TARGET_NAME STREQUAL "build-total")
            set(MSG "${BoldGreen}✓${ColorReset} ${BoldCyan}Total${ColorReset} build time: ${TIME_STR}")

            # Output using execute_process to preserve ANSI codes
            if(WIN32)
                # Use cmd.exe echo (fast) - Windows 10+ supports ANSI codes
                execute_process(COMMAND cmd /c "echo ${MSG}")
            else()
                execute_process(COMMAND printf "%b\n" "${MSG}")
            endif()
        else()
            set(MSG "${BoldGreen}✓${ColorReset} Built ${BoldCyan}${TARGET_NAME}${ColorReset} in ${TIME_STR}")

            # Output using execute_process to preserve ANSI codes
            if(WIN32)
                # Use cmd.exe echo (fast) - Windows 10+ supports ANSI codes
                execute_process(COMMAND cmd /c "echo ${MSG}")
            else()
                # Unix: Use printf to preserve escape sequences
                execute_process(COMMAND printf "%b\n" "${MSG}")
            endif()
        endif()

        # Create marker file to indicate POST_BUILD ran (prevents duplicate message from "check")
        set(MARKER_FILE "${CMAKE_BINARY_DIR}/.build_done_${TARGET_NAME}.txt")
        # Use END_TIME which is already in milliseconds
        file(WRITE "${MARKER_FILE}" "${END_TIME}")

        # Clean up time file
        file(REMOVE "${TIME_FILE}")
    else()
        # Fallback if time file doesn't exist
        set(MSG "${BoldGreen}✓${ColorReset} Built ${BoldCyan}${TARGET_NAME}${ColorReset}")

        if(WIN32)
            # Use cmd.exe echo (fast) - Windows 10+ supports ANSI codes
            execute_process(COMMAND cmd /c "echo ${MSG}")
        else()
            execute_process(COMMAND printf "%b\n" "${MSG}")
        endif()
    endif()
endif()
