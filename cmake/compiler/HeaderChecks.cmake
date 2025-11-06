# =============================================================================
# Header Availability Checks
# =============================================================================
# This module checks for platform-specific headers and defines preprocessor
# macros that can be used in the source code for conditional compilation.
#
# Checked headers:
#   - execinfo.h: Backtrace functions (glibc, libexecinfo)
#   - mach-o/dyld.h: macOS dyld functions
#
# Outputs:
#   - HAVE_EXECINFO_H: Set to 1 if execinfo.h is available
#   - HAVE_MACH_O_DYLD_H: Set to 1 if mach-o/dyld.h is available
# =============================================================================

include(CheckIncludeFile)

# Check for execinfo.h (backtrace functions)
# Available on: glibc (Linux), with libexecinfo (musl), macOS
# Not available on: Windows, some embedded systems
check_include_file(execinfo.h HAVE_EXECINFO_H)

if(HAVE_EXECINFO_H)
    message(STATUS "${BoldGreen}Found${ColorReset} header: ${BoldCyan}execinfo.h${ColorReset} (backtrace support)")
    add_compile_definitions(HAVE_EXECINFO_H=1)
else()
    message(STATUS "${BoldYellow}Missing${ColorReset} header: ${BoldCyan}execinfo.h${ColorReset} (backtrace unavailable)")
endif()

# Check for mach-o/dyld.h (macOS dyld functions)
# Available on: macOS only
# Used for: Getting executable path on macOS
if(APPLE)
    check_include_file(mach-o/dyld.h HAVE_MACH_O_DYLD_H)

    if(HAVE_MACH_O_DYLD_H)
        message(STATUS "${BoldGreen}Found${ColorReset} header: ${BoldCyan}mach-o/dyld.h${ColorReset} (macOS dyld)")
        add_compile_definitions(HAVE_MACH_O_DYLD_H=1)
    else()
        message(WARNING "${BoldRed}Missing${ColorReset} header: ${BoldCyan}mach-o/dyld.h${ColorReset} (required on macOS)")
    endif()
endif()
