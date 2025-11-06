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
# Tries multiple methods with fallbacks if tools aren't available
# Sets the output variable to the timestamp value
# =============================================================================
function(get_timestamp_ms OUTPUT_VAR)
    set(TIMESTAMP "")

    if(WIN32)
        # Windows: Try multiple methods

        # Method 1: PowerShell (most common)
        execute_process(
            COMMAND powershell -NoProfile -Command "(Get-Date).ToUniversalTime().Subtract((Get-Date '1970-01-01')).TotalMilliseconds"
            OUTPUT_VARIABLE TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE POWERSHELL_RESULT
        )

        if(NOT POWERSHELL_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 2: Try pwsh (PowerShell Core)
            execute_process(
                COMMAND pwsh -NoProfile -Command "(Get-Date).ToUniversalTime().Subtract((Get-Date '1970-01-01')).TotalMilliseconds"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PWSH_RESULT
            )
        endif()

        if(NOT PWSH_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 3: Try Python
            execute_process(
                COMMAND python -c "import time; print(int(time.time() * 1000))"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PYTHON_RESULT
            )
        endif()

        if(NOT PYTHON_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 4: Fallback to seconds with cmd.exe
            execute_process(
                COMMAND cmd /c "echo %time:~0,2%%time:~3,2%%time:~6,2%"
                OUTPUT_VARIABLE TIME_STR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            # This gives rough timestamp, multiply by 1000 for milliseconds
            # (Note: This is not epoch time, just for relative timing)
            if(NOT TIME_STR STREQUAL "")
                set(TIMESTAMP "${TIME_STR}000")
            endif()
        endif()
    elseif(APPLE)
        # macOS: BSD date doesn't support %N, try multiple methods

        # Method 1: Try Python3 (most accurate, milliseconds)
        execute_process(
            COMMAND python3 -c "import time; print(int(time.time() * 1000))"
            OUTPUT_VARIABLE TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE PYTHON3_RESULT
        )

        if(NOT PYTHON3_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 2: Try Python2 fallback
            execute_process(
                COMMAND python -c "import time; print(int(time.time() * 1000))"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PYTHON_RESULT
            )
        endif()

        if(NOT PYTHON_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 3: Try Perl (usually available on macOS)
            execute_process(
                COMMAND perl -MTime::HiRes=time -e "print int(time() * 1000)"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PERL_RESULT
            )
        endif()

        if(NOT PERL_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 4: Try Ruby (usually available on macOS)
            execute_process(
                COMMAND ruby -e "puts (Time.now.to_f * 1000).to_i"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE RUBY_RESULT
            )
        endif()

        if(NOT RUBY_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Method 5: Fallback to seconds only (multiply by 1000)
            execute_process(
                COMMAND date +%s
                OUTPUT_VARIABLE TIMESTAMP_SEC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT TIMESTAMP_SEC STREQUAL "")
                math(EXPR TIMESTAMP "${TIMESTAMP_SEC} * 1000")
            endif()
        endif()
    else()
        # Linux: Use date with milliseconds
        execute_process(
            COMMAND date +%s%3N
            OUTPUT_VARIABLE TIMESTAMP
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE DATE_RESULT
        )

        if(NOT DATE_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Fallback: Try Python
            execute_process(
                COMMAND python3 -c "import time; print(int(time.time() * 1000))"
                OUTPUT_VARIABLE TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE PYTHON3_RESULT
            )
        endif()

        if(NOT PYTHON3_RESULT EQUAL 0 OR TIMESTAMP STREQUAL "")
            # Fallback: Try seconds only
            execute_process(
                COMMAND date +%s
                OUTPUT_VARIABLE TIMESTAMP_SEC
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(NOT TIMESTAMP_SEC STREQUAL "")
                math(EXPR TIMESTAMP "${TIMESTAMP_SEC} * 1000")
            endif()
        endif()
    endif()

    # Final fallback: If all methods failed, use a fixed value
    if(TIMESTAMP STREQUAL "")
        set(TIMESTAMP "0")
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

        # Handle decimal point in PowerShell output
        string(REGEX REPLACE "\\.[0-9]+$" "" MARKER_TIME_INT "${MARKER_TIME}")
        string(REGEX REPLACE "\\.[0-9]+$" "" CURRENT_TIME_INT "${CURRENT_TIME}")
        math(EXPR TIME_DIFF_MS "${CURRENT_TIME_INT} - ${MARKER_TIME_INT}")

        # If marker is fresh (< 2000 milliseconds old), POST_BUILD just ran, skip message
        if(TIME_DIFF_MS LESS 2000)
            set(SHOW_MESSAGE FALSE)
        endif()
    endif()

    if(SHOW_MESSAGE)
        # Target was up-to-date, show success message with "(up to date)" in green
        set(MSG "${BoldGreen}✓${ColorReset} Built ${BoldCyan}${TARGET_NAME}${ColorReset} ${BoldGreen}(up to date)${ColorReset}")

        if(WIN32)
            execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG}')")
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
        # Handle decimal point in PowerShell output
        string(REGEX REPLACE "\\.[0-9]+$" "" START_TIME_INT "${START_TIME}")
        string(REGEX REPLACE "\\.[0-9]+$" "" END_TIME_INT "${END_TIME}")
        math(EXPR ELAPSED_MS "${END_TIME_INT} - ${START_TIME_INT}")

        # Convert to seconds and milliseconds
        math(EXPR ELAPSED_SEC "${ELAPSED_MS} / 1000")
        math(EXPR MS_ONLY "${ELAPSED_MS} % 1000")

        # Format elapsed time
        if(ELAPSED_SEC LESS 60)
            if(ELAPSED_SEC LESS 7)
                set(TIME_STR "${BoldGreen}${ELAPSED_SEC}.${MS_ONLY}s${ColorReset}")
            else()
                set(TIME_STR "${BoldYellow}${ELAPSED_SEC}.${MS_ONLY}s${ColorReset}")
            endif()
        else()
            math(EXPR MINUTES "${ELAPSED_SEC} / 60")
            math(EXPR SECONDS "${ELAPSED_SEC} % 60")
            set(TIME_STR "${BoldMagenta}${MINUTES}m ${SECONDS}.${MS_ONLY}s${ColorReset}")
        endif()

        # Print success message with colored timing
        # Format message string with ANSI codes
        # Special formatting for build-total target
        if(TARGET_NAME STREQUAL "build-total")
            set(MSG "${BoldGreen}✓${ColorReset} ${BoldCyan}Total${ColorReset} build time: ${TIME_STR}")

            # Output using execute_process to preserve ANSI codes
            if(WIN32)
                execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG}')")
            else()
                execute_process(COMMAND printf "%b\n" "${MSG}")
            endif()
        else()
            set(MSG "${BoldGreen}✓${ColorReset} Built ${BoldCyan}${TARGET_NAME}${ColorReset} in ${TIME_STR}")

            # Output using execute_process to preserve ANSI codes
            if(WIN32)
                # Windows: Use PowerShell Console.WriteLine to output raw ANSI codes
                execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG}')")
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
            execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG}')")
        else()
            execute_process(COMMAND printf "%b\n" "${MSG}")
        endif()
    endif()
endif()
