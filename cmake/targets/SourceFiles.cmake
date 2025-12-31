# =============================================================================
# Source Files Module
# =============================================================================
# This module collects all source files for each library module by including
# modular source definitions. Each module is defined in its own cmake file.
#
# Prerequisites:
#   - Platform detection complete (WIN32, PLATFORM_DARWIN, etc.)
#   - USE_MUSL known
#   - SIMD detection complete (ENABLE_SIMD_* variables)
#
# Outputs:
#   - UTIL_SRCS
#   - CRYPTO_SRCS
#   - PLATFORM_SRCS
#   - SIMD_SRCS
#   - VIDEO_SRCS
#   - AUDIO_SRCS
#   - NETWORK_SRCS
#   - CORE_SRCS
#   - DATA_STRUCTURES_SRCS
#   - APP_SRCS
#   - ACDS_SRCS
#   - TOOLING_PANIC_SRCS
#   - TOOLING_PANIC_REPORT_SRCS
# =============================================================================

# Include modular source definitions
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceUtil.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceCrypto.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourcePlatform.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceSIMD.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceVideo.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceAudio.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceNetwork.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceCore.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceDataStructures.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/source-modules/SourceApplication.cmake)
