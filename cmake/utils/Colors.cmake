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
# =============================================================================

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
