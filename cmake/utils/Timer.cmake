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
# Uses CMake's native string(TIMESTAMP) - no external process spawn needed!
# Sets the output variable to the timestamp value
# =============================================================================
function(get_timestamp_ms OUTPUT_VAR)
    # Use CMake's native timestamp (seconds since epoch) - instant, no process spawn
    # This is available in CMake 2.8.11+ and is extremely fast
    string(TIMESTAMP EPOCH_SEC "%s" UTC)

    # Convert to milliseconds (we don't have sub-second precision, but that's fine for build timing)
    math(EXPR TIMESTAMP "${EPOCH_SEC} * 1000")

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
