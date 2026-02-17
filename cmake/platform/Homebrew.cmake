# =============================================================================
# Homebrew.cmake - Homebrew Package Manager Integration
# =============================================================================
# Re-exports HOMEBREW_PREFIX which is detected in FindPrograms.cmake.
# Included after FindPrograms.cmake for modules that want to check availability.
#
# Available after this include:
#   HOMEBREW_PREFIX            - Homebrew installation prefix (e.g., /opt/homebrew, /usr/local)
#                                Empty string if Homebrew is not installed.
#   ASCIICHAT_BREW_EXECUTABLE - Path to brew binary (from FindPrograms.cmake)
#
# HOMEBREW_PREFIX is detected by FindPrograms.cmake via `brew --prefix`.
# This file exists as documentation and as a guard for modules that depend on it.
# =============================================================================

if(NOT DEFINED HOMEBREW_PREFIX)
    message(WARNING "Homebrew.cmake included before FindPrograms.cmake — HOMEBREW_PREFIX not set")
endif()
