# =============================================================================
# Font Embedding Targets
# =============================================================================
# Centralized configuration for embedding bundled fonts as C arrays
#
# This module handles all font-related build configuration:
# - Font directory creation
# - Matrix-Resurrected.ttf embedding (special-purpose font)
# - DejaVu Sans Mono embedding (fallback/default font)
# =============================================================================

# Create font and generated directories for font embedding
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/fonts")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/data/fonts")

# =============================================================================
# Matrix Font Embedding
# =============================================================================
# Downloads and embeds the Matrix-Resurrected.ttf font as a C array
# Used as a special-purpose font for themes like digital rain

set(MATRIX_FONT_URL "https://github.com/Rezmason/matrix/raw/master/assets/Matrix-Resurrected.ttf")
set(MATRIX_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/Matrix-Resurrected.ttf")
set(MATRIX_FONT_GEN "${CMAKE_BINARY_DIR}/generated/data/fonts/matrix_resurrected.c")

# Download font at configure time
if(NOT EXISTS "${MATRIX_FONT_SRC}")
    message(STATUS "Downloading Matrix-Resurrected.ttf...")
    file(DOWNLOAD "${MATRIX_FONT_URL}" "${MATRIX_FONT_SRC}" SHOW_PROGRESS TLS_VERIFY ON)
endif()

# Convert to C array at configure time
if(NOT EXISTS "${MATRIX_FONT_GEN}" OR "${MATRIX_FONT_SRC}" IS_NEWER_THAN "${MATRIX_FONT_GEN}")
    message(STATUS "Embedding Matrix-Resurrected.ttf as C array...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                "-DINPUT=${MATRIX_FONT_SRC}"
                "-DOUTPUT=${MATRIX_FONT_GEN}"
                "-DVAR_NAME=g_font_matrix_resurrected"
                "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
        RESULT_VARIABLE BIN2C_RESULT
    )
    if(NOT BIN2C_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to convert font to C array")
    endif()
endif()

# =============================================================================
# Default Font Embedding (DejaVu Sans Mono)
# =============================================================================
# Downloads and embeds DejaVuSansMono.ttf as a C array for fallback
# This ensures render-file works even if system fonts are unavailable

set(DEFAULT_FONT_TARBALL_URL "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2")
set(DEFAULT_FONT_TARBALL "${CMAKE_BINARY_DIR}/fonts/dejavu-fonts-ttf-2.37.tar.bz2")
set(DEFAULT_FONT_SRC "${CMAKE_BINARY_DIR}/fonts/DejaVuSansMono.ttf")
set(DEFAULT_FONT_GEN "${CMAKE_BINARY_DIR}/generated/data/fonts/default.c")

# Download and extract font tarball at configure time
if(NOT EXISTS "${DEFAULT_FONT_SRC}")
    if(NOT EXISTS "${DEFAULT_FONT_TARBALL}")
        message(STATUS "Downloading DejaVu fonts tarball...")
        file(DOWNLOAD "${DEFAULT_FONT_TARBALL_URL}" "${DEFAULT_FONT_TARBALL}"
             SHOW_PROGRESS
             TLS_VERIFY ON
             STATUS DOWNLOAD_STATUS)
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_ERROR)
        if(NOT DOWNLOAD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to download DejaVu fonts tarball: ${DOWNLOAD_ERROR}")
        endif()
    endif()

    message(STATUS "Extracting DejaVuSansMono.ttf from tarball...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf "${DEFAULT_FONT_TARBALL}" "dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/fonts"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract DejaVuSansMono.ttf from tarball")
    endif()

    # Move extracted file to the right place
    file(RENAME "${CMAKE_BINARY_DIR}/fonts/dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf" "${DEFAULT_FONT_SRC}")
endif()

# Create custom commands to generate C arrays at build time
# This ensures --clean-first regenerates the files automatically
add_custom_command(
    OUTPUT "${MATRIX_FONT_GEN}"
    COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${MATRIX_FONT_SRC}"
            "-DOUTPUT=${MATRIX_FONT_GEN}"
            "-DVAR_NAME=g_font_matrix_resurrected"
            "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
    DEPENDS "${MATRIX_FONT_SRC}"
    COMMENT "Embedding Matrix-Resurrected.ttf as C array"
    VERBATIM
)

add_custom_command(
    OUTPUT "${DEFAULT_FONT_GEN}"
    COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${DEFAULT_FONT_SRC}"
            "-DOUTPUT=${DEFAULT_FONT_GEN}"
            "-DVAR_NAME=g_font_default"
            "-P" "${CMAKE_SOURCE_DIR}/cmake/tools/bin2c.cmake"
    DEPENDS "${DEFAULT_FONT_SRC}"
    COMMENT "Embedding DejaVuSansMono.ttf as C array"
    VERBATIM
)

# Create custom target to ensure fonts are built before main targets
add_custom_target(
    generate_fonts ALL
    DEPENDS "${MATRIX_FONT_GEN}" "${DEFAULT_FONT_GEN}"
)
