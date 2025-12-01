# =============================================================================
# Colors Module
# =============================================================================
# Defines ANSI color escape sequences for colored terminal output
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/utils/Colors.cmake)
#
# Available colors:
#   - ColorReset: Reset all formatting
#   - BoldGreen, BoldYellow, BoldRed, BoldBlue, BoldMagenta, BoldCyan
#   - Green, Yellow, Red, Blue, Magenta, Cyan
#
# Color control:
#   Colors are ON by default.
#   Colors disabled when:
#   - CLAUDECODE env var is set (for LLM/automation contexts)
#   - NO_COLOR env var is set (https://no-color.org/)
#   - CLICOLOR=0 is set
#   - TERM=dumb
#   CLICOLOR_FORCE=1 overrides everything and forces colors ON.
# =============================================================================

# Default: colors ON
set(_colors_enabled TRUE)

# Disable for LLM/automation (Claude Code sets CLAUDECODE)
if(DEFINED ENV{CLAUDECODE})
  set(_colors_enabled FALSE)
endif()

# NO_COLOR (https://no-color.org/)
if(DEFINED ENV{NO_COLOR})
  set(_colors_enabled FALSE)
endif()

# CLICOLOR=0
if(DEFINED ENV{CLICOLOR} AND "$ENV{CLICOLOR}" STREQUAL "0")
  set(_colors_enabled FALSE)
endif()

# TERM=dumb
if(DEFINED ENV{TERM} AND "$ENV{TERM}" STREQUAL "dumb")
  set(_colors_enabled FALSE)
endif()

# CLICOLOR_FORCE overrides everything
if(DEFINED ENV{CLICOLOR_FORCE} AND NOT "$ENV{CLICOLOR_FORCE}" STREQUAL "0" AND NOT "$ENV{CLICOLOR_FORCE}" STREQUAL "")
  set(_colors_enabled TRUE)
endif()

if(_colors_enabled)
  # Define ANSI escape character
  string(ASCII 27 Esc)

  # Reset
  set(ColorReset "${Esc}[m")

  # Bold colors
  set(BoldGreen "${Esc}[1;32m")
  set(BoldYellow "${Esc}[1;33m")
  set(BoldRed "${Esc}[1;31m")
  set(BoldBlue "${Esc}[1;34m")
  set(BoldMagenta "${Esc}[1;35m")
  set(BoldCyan "${Esc}[1;36m")

  # Regular colors
  set(Green "${Esc}[32m")
  set(Yellow "${Esc}[33m")
  set(Red "${Esc}[31m")
  set(Blue "${Esc}[34m")
  set(Magenta "${Esc}[35m")
  set(Cyan "${Esc}[36m")
else()
  # No color support - set all to empty strings
  set(ColorReset "")
  set(BoldGreen "")
  set(BoldYellow "")
  set(BoldRed "")
  set(BoldBlue "")
  set(BoldMagenta "")
  set(BoldCyan "")
  set(Green "")
  set(Yellow "")
  set(Red "")
  set(Blue "")
  set(Magenta "")
  set(Cyan "")
endif()

unset(_colors_enabled)
