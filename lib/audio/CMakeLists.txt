# lib/audio - Audio processing

if(PLATFORM_IOS)
    set(AUDIO_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/ios_stub.c
        ${CMAKE_CURRENT_SOURCE_DIR}/audio.c
        ${CMAKE_CURRENT_SOURCE_DIR}/test_pattern.c
        ${CMAKE_CURRENT_SOURCE_DIR}/recording.c
        ${CMAKE_CURRENT_SOURCE_DIR}/analysis.c
        ${CMAKE_CURRENT_SOURCE_DIR}/processor.c
        ${CMAKE_CURRENT_SOURCE_DIR}/format.c
        ${CMAKE_CURRENT_SOURCE_DIR}/effects.c
        ${CMAKE_CURRENT_SOURCE_DIR}/spectral.c
        ${CMAKE_CURRENT_SOURCE_DIR}/visualization.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mixer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/wav_writer.c
        ${CMAKE_CURRENT_SOURCE_DIR}/opus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/client_pipeline.cpp)
    list(APPEND AUDIO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/ios_portaudio.m)
else()
set(AUDIO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_pattern.c
    ${CMAKE_CURRENT_SOURCE_DIR}/processor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/effects.c
    ${CMAKE_CURRENT_SOURCE_DIR}/spectral.c
    ${CMAKE_CURRENT_SOURCE_DIR}/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/recording.c
    ${CMAKE_CURRENT_SOURCE_DIR}/visualization.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mixer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wav_writer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/opus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client_pipeline.cpp
)
endif()

set(AUDIO_SRCS ${AUDIO_SRCS} PARENT_SCOPE)

if(PLATFORM_IOS AND TARGET AudioProcess)
    target_sources(AudioProcess PRIVATE
        ${CMAKE_BINARY_DIR}/_deps/legacy/Debug/asciichat_aec3-src/audio_processing/resampler/sinc_resampler_sse.cc
        ${CMAKE_BINARY_DIR}/_deps/legacy/Debug/asciichat_aec3-src/audio_processing/utility/ooura_fft_sse2.cc)
endif()

# Preserve non-finite input checks even in Release configurations using fast math.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/processor.c ${CMAKE_CURRENT_SOURCE_DIR}/spectral.c ${CMAKE_CURRENT_SOURCE_DIR}/format.c PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
