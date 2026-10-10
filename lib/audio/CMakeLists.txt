# lib/audio - Audio processing

set(AUDIO_SRCS
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

set(AUDIO_SRCS ${AUDIO_SRCS} PARENT_SCOPE)

# Preserve non-finite input checks even in Release configurations using fast math.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/spectral.c PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
