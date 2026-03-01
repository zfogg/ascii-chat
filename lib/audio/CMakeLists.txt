# lib/audio - Audio processing

set(AUDIO_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mixer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/wav_writer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/opus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/analysis.c
    ${CMAKE_CURRENT_SOURCE_DIR}/client_pipeline.cpp
)

set(AUDIO_SRCS ${AUDIO_SRCS} PARENT_SCOPE)
