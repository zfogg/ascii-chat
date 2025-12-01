# =============================================================================
# Symbol Validation Utility
# =============================================================================
# Validates that expected symbols exist in built libraries using llvm-nm
#
# Usage:
#   cmake -DLLVM_NM=<path> -DLIBRARY=<path> -DSYMBOLS="sym1,sym2,sym3" -P ValidateSymbols.cmake
#
# Parameters:
#   LLVM_NM  - Path to llvm-nm executable
#   LIBRARY  - Path to the library file to check
#   SYMBOLS  - Comma-separated list of required symbol names
#
# Returns:
#   Exits with error if any symbol is missing
# =============================================================================

if(NOT LLVM_NM)
    message(FATAL_ERROR "LLVM_NM not specified")
endif()

if(NOT LIBRARY)
    message(FATAL_ERROR "LIBRARY not specified")
endif()

if(NOT SYMBOLS)
    message(FATAL_ERROR "SYMBOLS not specified")
endif()

# Convert comma-separated to list
string(REPLACE "," ";" SYMBOLS "${SYMBOLS}")

if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "Library not found: ${LIBRARY}")
endif()

# Run llvm-nm to get all symbols
execute_process(
    COMMAND "${LLVM_NM}" --defined-only "${LIBRARY}"
    OUTPUT_VARIABLE NM_OUTPUT
    ERROR_VARIABLE NM_ERROR
    RESULT_VARIABLE NM_RESULT
)

if(NOT NM_RESULT EQUAL 0)
    message(FATAL_ERROR "llvm-nm failed on ${LIBRARY}:\n${NM_ERROR}")
endif()

# Check each required symbol
set(MISSING_SYMBOLS "")
foreach(SYMBOL ${SYMBOLS})
    # Symbol should appear as " T symbol" or " D symbol" etc. in nm output
    # Use simple string FIND - symbol names are unique enough
    # Look for " T symbolname" or " D symbolname" pattern
    string(FIND "${NM_OUTPUT}" " T ${SYMBOL}" FOUND_T)
    string(FIND "${NM_OUTPUT}" " D ${SYMBOL}" FOUND_D)
    string(FIND "${NM_OUTPUT}" " B ${SYMBOL}" FOUND_B)
    string(FIND "${NM_OUTPUT}" " R ${SYMBOL}" FOUND_R)

    if(FOUND_T EQUAL -1 AND FOUND_D EQUAL -1 AND FOUND_B EQUAL -1 AND FOUND_R EQUAL -1)
        list(APPEND MISSING_SYMBOLS "${SYMBOL}")
    endif()
endforeach()

if(MISSING_SYMBOLS)
    message(FATAL_ERROR "Symbol validation FAILED for ${LIBRARY}\n"
        "Missing symbols: ${MISSING_SYMBOLS}\n"
        "Run 'llvm-nm --defined-only ${LIBRARY}' to see available symbols")
endif()

get_filename_component(LIB_NAME "${LIBRARY}" NAME)
message(STATUS "Symbol validation PASSED for ${LIB_NAME}: all ${SYMBOLS} found")
