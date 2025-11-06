# =============================================================================
# Build Timer Script
# =============================================================================
# Records build start/end time and prints colored timing message
#
# Usage:
#   cmake -DACTION=start -DTARGET_NAME=<name> -P build_timer.cmake
#   cmake -DACTION=end -DTARGET_NAME=<name> -P build_timer.cmake
#
# Requires:
#   - ACTION: "start" or "end"
#   - TARGET_NAME: Name of the target being built
# =============================================================================

# Import color definitions from Colors.cmake
include(${SOURCE_DIR}/cmake/utils/Colors.cmake)

# Time file path
set(TIME_FILE "${CMAKE_BINARY_DIR}/.build_time_${TARGET_NAME}.txt")

if(ACTION STREQUAL "start")
    # Record start time with millisecond precision
    if(WIN32)
        # Windows: Use PowerShell to get milliseconds since epoch
        execute_process(
            COMMAND powershell -NoProfile -Command "(Get-Date).ToUniversalTime().Subtract((Get-Date '1970-01-01')).TotalMilliseconds"
            OUTPUT_VARIABLE START_TIME
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else()
        # Unix: Use date with milliseconds
        execute_process(
            COMMAND date +%s%3N
            OUTPUT_VARIABLE START_TIME
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()
    file(WRITE "${TIME_FILE}" "${START_TIME}")
elseif(ACTION STREQUAL "check")
    # Show success message only if target was NOT just rebuilt (POST_BUILD didn't run)
    # Check if the marker file exists and is fresh (created within last 2 seconds)
    set(MARKER_FILE "${CMAKE_BINARY_DIR}/.build_done_${TARGET_NAME}.txt")
    set(SHOW_MESSAGE TRUE)

    if(EXISTS "${MARKER_FILE}")
        file(READ "${MARKER_FILE}" MARKER_TIME)

        # Get current time in milliseconds
        if(WIN32)
            execute_process(
                COMMAND powershell -NoProfile -Command "(Get-Date).ToUniversalTime().Subtract((Get-Date '1970-01-01')).TotalMilliseconds"
                OUTPUT_VARIABLE CURRENT_TIME
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            execute_process(
                COMMAND date +%s%3N
                OUTPUT_VARIABLE CURRENT_TIME
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()

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
    # Calculate elapsed time with millisecond precision
    if(EXISTS "${TIME_FILE}")
        file(READ "${TIME_FILE}" START_TIME)

        # Get end time with millisecond precision
        if(WIN32)
            execute_process(
                COMMAND powershell -NoProfile -Command "(Get-Date).ToUniversalTime().Subtract((Get-Date '1970-01-01')).TotalMilliseconds"
                OUTPUT_VARIABLE END_TIME
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            execute_process(
                COMMAND date +%s%3N
                OUTPUT_VARIABLE END_TIME
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        endif()

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
            set(MSG2 "${BoldGreen}✓${ColorReset} ${BoldCyan}Total${ColorReset} build time: ${TIME_STR}")

            # Output using execute_process to preserve ANSI codes
            if(WIN32)
                execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG}')")
                execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG2}')")
                execute_process(COMMAND powershell -NoProfile -Command "[Console]::WriteLine('${MSG3}')")
            else()
                execute_process(COMMAND printf "%b\n" "${MSG}")
                execute_process(COMMAND printf "%b\n" "${MSG2}")
                execute_process(COMMAND printf "%b\n" "${MSG3}")
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
