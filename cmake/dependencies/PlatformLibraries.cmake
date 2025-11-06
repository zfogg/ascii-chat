# =============================================================================
# Platform-Specific System Libraries Configuration
# =============================================================================
# Configures platform-specific system libraries and frameworks
#
# Platform-specific library detection:
#   - Windows: Windows system libraries (ws2_32, user32, advapi32, etc.)
#   - macOS: Apple frameworks (Foundation, AVFoundation, CoreMedia, CoreVideo)
#   - Linux: Threads, Linux-specific libraries
#
# Prerequisites (must be set before including this file):
#   - WIN32, PLATFORM_DARWIN, PLATFORM_LINUX: Platform detection variables
#   - CRITERION_FOUND: Whether Criterion test framework was found
#
# Outputs (variables set by this file):
#   Windows:
#     - WS2_32_LIB, USER32_LIB, ADVAPI32_LIB, DBGHELP_LIB: System libraries
#     - MF_LIB, MFPLAT_LIB, MFREADWRITE_LIB, MFUUID_LIB: Media Foundation libraries
#     - OLE32_LIB: OLE library
#   macOS:
#     - FOUNDATION_FRAMEWORK, AVFOUNDATION_FRAMEWORK
#     - COREMEDIA_FRAMEWORK, COREVIDEO_FRAMEWORK
#   Linux:
#     - CMAKE_THREAD_LIBS_INIT: Thread library
#     - Plus Criterion test dependencies (if CRITERION_FOUND)
# =============================================================================

if(WIN32)
    # =============================================================================
    # Windows System Libraries
    # =============================================================================
    # Standard Windows system libraries for native builds
    set(WS2_32_LIB ws2_32)        # Windows Sockets 2 (networking)
    set(USER32_LIB user32)        # Windows User Interface
    set(ADVAPI32_LIB advapi32)    # Advanced Windows API (registry, crypto, etc.)
    set(DBGHELP_LIB dbghelp)      # Debug help library (stack traces, symbols)
    set(OLE32_LIB ole32)          # Object Linking and Embedding

    # Media Foundation libraries (for webcam support)
    set(MF_LIB mf)                # Media Foundation core
    set(MFPLAT_LIB mfplat)        # Media Foundation platform
    set(MFREADWRITE_LIB mfreadwrite)  # Media Foundation read/write
    set(MFUUID_LIB mfuuid)        # Media Foundation UUIDs

else()
    # =============================================================================
    # Unix-Like Platform Libraries (macOS/Linux)
    # =============================================================================

    if(PLATFORM_DARWIN)
        # =============================================================================
        # macOS Frameworks
        # =============================================================================
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
        find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
        find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)

        # Audio frameworks (required for static portaudio)
        find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
        find_library(AUDIOUNIT_FRAMEWORK AudioUnit REQUIRED)
        find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
        find_library(CORESERVICES_FRAMEWORK CoreServices REQUIRED)

    elseif(PLATFORM_LINUX)
        # =============================================================================
        # Linux System Libraries
        # =============================================================================
        find_package(Threads REQUIRED)

        # Linux library search paths (matches Makefile)
        link_directories(/usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu)
    endif()
endif()
