# =============================================================================
# Module 6: Audio Processing (changes weekly)
# =============================================================================
# Audio capture, mixing, and codec handling

set(AUDIO_SRCS
    lib/audio/audio.c
    lib/audio/mixer.c
    lib/audio/wav_writer.c
    lib/audio/opus_codec.c
    lib/audio/analysis.c
    lib/audio/client_audio_pipeline.cpp
)

# CRITICAL: client_audio_pipeline.cpp includes WebRTC headers which require C++17.
# The main ascii-chat project uses C++26, so we must override the C++ standard
# for just this file to avoid Abseil's std::result_of errors in C++26.
set_source_files_properties(
    lib/audio/client_audio_pipeline.cpp
    PROPERTIES
    COMPILE_FLAGS "-std=c++17"
    LANGUAGE CXX
)
