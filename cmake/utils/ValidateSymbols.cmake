# =============================================================================
# Symbol Validation Utility
# =============================================================================
# Validates that expected symbols exist using the selected toolchain's symbol tool.
#
# Usage:
#   cmake -DSYMBOL_TOOL=<path> -DLIBRARY=<path> -DSYMBOLS="sym1,sym2,sym3" \
#         [-DINTERNAL_SYMBOLS="sym4,sym5"] -P ValidateSymbols.cmake
#
# Parameters:
#   SYMBOL_TOOL      - Path to the selected symbol/export tool
#   LIBRARY          - Path to the library file to check
#   SYMBOLS          - Comma-separated list of required exported symbol names (T/D/B/R)
#   INTERNAL_SYMBOLS - (Optional) Comma-separated list of internal symbols (t/d/b/r)
#                      These are verified to be linked but not necessarily exported
#
# Returns:
#   Exits with error if any symbol is missing
# =============================================================================

if(NOT SYMBOL_TOOL)
    message(FATAL_ERROR "SYMBOL_TOOL not specified")
endif()

if(NOT LIBRARY)
    message(FATAL_ERROR "LIBRARY not specified")
endif()

# SYMBOLS can be empty if only checking INTERNAL_SYMBOLS
if(NOT SYMBOLS AND NOT INTERNAL_SYMBOLS)
    message(FATAL_ERROR "Either SYMBOLS or INTERNAL_SYMBOLS must be specified")
endif()

# Convert comma-separated to list
if(SYMBOLS)
    string(REPLACE "," ";" SYMBOLS "${SYMBOLS}")
endif()
if(INTERNAL_SYMBOLS)
    string(REPLACE "," ";" INTERNAL_SYMBOLS "${INTERNAL_SYMBOLS}")
endif()

if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "Library not found: ${LIBRARY}")
endif()

# Run the active platform's symbol tool. MSVC's dumpbin reports exports from
# the DLL, while llvm-nm reports defined symbols from Unix objects/libraries.
get_filename_component(_symbol_tool_name "${SYMBOL_TOOL}" NAME)
if(_symbol_tool_name MATCHES "^(dumpbin|dumpbin\.exe)$")
    get_filename_component(_library_extension "${LIBRARY}" EXT)
    if(_library_extension MATCHES "^[.]dll$")
        set(_dumpbin_mode /exports)
    else()
        # CMake's TARGET_LINKER_FILE is an MSVC import library (.lib) for a
        # DLL target.  /exports only accepts the PE image; /linkermember is
        # the compiler-native listing for the corresponding import library.
        set(_dumpbin_mode /linkermember:1)
    endif()
    execute_process(
        COMMAND "${SYMBOL_TOOL}" ${_dumpbin_mode} "${LIBRARY}"
        OUTPUT_VARIABLE NM_OUTPUT
        ERROR_VARIABLE NM_ERROR
        RESULT_VARIABLE NM_RESULT)
    set(_symbol_export_format TRUE)
else()
    execute_process(
        COMMAND "${SYMBOL_TOOL}" --defined-only "${LIBRARY}"
        OUTPUT_VARIABLE NM_OUTPUT
        ERROR_VARIABLE NM_ERROR
        RESULT_VARIABLE NM_RESULT)
    set(_symbol_export_format FALSE)
endif()

if(NOT NM_RESULT EQUAL 0)
    message(FATAL_ERROR "${SYMBOL_TOOL} failed on ${LIBRARY}:\n${NM_ERROR}")
endif()

# Check each required exported symbol (uppercase T/D/B/R = global/exported)
set(MISSING_SYMBOLS "")
foreach(SYMBOL ${SYMBOLS})
    if(_symbol_export_format)
        string(REGEX MATCH "(^|[\r\n ])${SYMBOL}([\r\n ]|$)" _found_export "${NM_OUTPUT}")
        if(_found_export)
            set(FOUND_EXPORT 0)
        else()
            set(FOUND_EXPORT -1)
        endif()
        if(FOUND_EXPORT EQUAL -1)
            list(APPEND MISSING_SYMBOLS "${SYMBOL}")
        endif()
        continue()
    endif()
    # Symbol should appear as " T symbol" or " D symbol" etc. in nm output
    # Use simple string FIND - symbol names are unique enough
    # Look for " T symbolname" or " D symbolname" pattern
    # On macOS, symbols have underscore prefix (_symbol), so check both
    string(FIND "${NM_OUTPUT}" " T ${SYMBOL}" FOUND_T)
    string(FIND "${NM_OUTPUT}" " D ${SYMBOL}" FOUND_D)
    string(FIND "${NM_OUTPUT}" " B ${SYMBOL}" FOUND_B)
    string(FIND "${NM_OUTPUT}" " R ${SYMBOL}" FOUND_R)
    # macOS underscore-prefixed symbols
    string(FIND "${NM_OUTPUT}" " T _${SYMBOL}" FOUND_T_MAC)
    string(FIND "${NM_OUTPUT}" " D _${SYMBOL}" FOUND_D_MAC)
    string(FIND "${NM_OUTPUT}" " B _${SYMBOL}" FOUND_B_MAC)
    string(FIND "${NM_OUTPUT}" " R _${SYMBOL}" FOUND_R_MAC)

    if(FOUND_T EQUAL -1 AND FOUND_D EQUAL -1 AND FOUND_B EQUAL -1 AND FOUND_R EQUAL -1 AND
       FOUND_T_MAC EQUAL -1 AND FOUND_D_MAC EQUAL -1 AND FOUND_B_MAC EQUAL -1 AND FOUND_R_MAC EQUAL -1)
        list(APPEND MISSING_SYMBOLS "${SYMBOL}")
    endif()
endforeach()

if(MISSING_SYMBOLS)
    message(FATAL_ERROR "Symbol validation FAILED for ${LIBRARY}\n"
        "Missing exported symbols: ${MISSING_SYMBOLS}\n"
        "Run '${SYMBOL_TOOL} --defined-only ${LIBRARY}' to see available symbols")
endif()

# Check each required internal symbol (lowercase t/d/b/r = local/internal)
# These symbols are linked into the library but not exported publicly
set(MISSING_INTERNAL_SYMBOLS "")
foreach(SYMBOL ${INTERNAL_SYMBOLS})
    # Check for both exported (T) and internal (t) symbols - either is acceptable
    # Internal symbols use lowercase: t (text), d (data), b (bss), r (read-only)
    string(FIND "${NM_OUTPUT}" " T ${SYMBOL}" FOUND_T)
    string(FIND "${NM_OUTPUT}" " t ${SYMBOL}" FOUND_t)
    string(FIND "${NM_OUTPUT}" " D ${SYMBOL}" FOUND_D)
    string(FIND "${NM_OUTPUT}" " d ${SYMBOL}" FOUND_d)
    string(FIND "${NM_OUTPUT}" " B ${SYMBOL}" FOUND_B)
    string(FIND "${NM_OUTPUT}" " b ${SYMBOL}" FOUND_b)
    string(FIND "${NM_OUTPUT}" " R ${SYMBOL}" FOUND_R)
    string(FIND "${NM_OUTPUT}" " r ${SYMBOL}" FOUND_r)
    # macOS underscore-prefixed symbols
    string(FIND "${NM_OUTPUT}" " T _${SYMBOL}" FOUND_T_MAC)
    string(FIND "${NM_OUTPUT}" " t _${SYMBOL}" FOUND_t_MAC)
    string(FIND "${NM_OUTPUT}" " D _${SYMBOL}" FOUND_D_MAC)
    string(FIND "${NM_OUTPUT}" " d _${SYMBOL}" FOUND_d_MAC)
    string(FIND "${NM_OUTPUT}" " B _${SYMBOL}" FOUND_B_MAC)
    string(FIND "${NM_OUTPUT}" " b _${SYMBOL}" FOUND_b_MAC)
    string(FIND "${NM_OUTPUT}" " R _${SYMBOL}" FOUND_R_MAC)
    string(FIND "${NM_OUTPUT}" " r _${SYMBOL}" FOUND_r_MAC)

    if(FOUND_T EQUAL -1 AND FOUND_t EQUAL -1 AND FOUND_D EQUAL -1 AND FOUND_d EQUAL -1 AND
       FOUND_B EQUAL -1 AND FOUND_b EQUAL -1 AND FOUND_R EQUAL -1 AND FOUND_r EQUAL -1 AND
       FOUND_T_MAC EQUAL -1 AND FOUND_t_MAC EQUAL -1 AND FOUND_D_MAC EQUAL -1 AND FOUND_d_MAC EQUAL -1 AND
       FOUND_B_MAC EQUAL -1 AND FOUND_b_MAC EQUAL -1 AND FOUND_R_MAC EQUAL -1 AND FOUND_r_MAC EQUAL -1)
        list(APPEND MISSING_INTERNAL_SYMBOLS "${SYMBOL}")
    endif()
endforeach()

if(MISSING_INTERNAL_SYMBOLS)
    message(FATAL_ERROR "Symbol validation FAILED for ${LIBRARY}\n"
        "Missing internal symbols: ${MISSING_INTERNAL_SYMBOLS}\n"
        "Run '${SYMBOL_TOOL} --defined-only ${LIBRARY}' to see available symbols")
endif()

