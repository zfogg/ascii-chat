# lib/audio - Audio processing

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

set(AUDIO_SRCS ${AUDIO_SRCS} PARENT_SCOPE)

if(BUILD_SHARED_LIBS)
    set(ASCIICHAT_RNNOISE_LIBRARY_TYPE SHARED)
else()
    set(ASCIICHAT_RNNOISE_LIBRARY_TYPE STATIC)
endif()

add_library(ascii-chat-rnnoise ${ASCIICHAT_RNNOISE_LIBRARY_TYPE}
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/denoise.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/rnn.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/rnn_data.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/rnn_reader.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/pitch.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/kiss_fft.c
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src/celt_lpc.c
)
set_target_properties(ascii-chat-rnnoise PROPERTIES OUTPUT_NAME rnnoise)
set_target_properties(ascii-chat-rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(ascii-chat-rnnoise PRIVATE RNNOISE_BUILD=1 M_PI=3.14159265358979323846)
if(WIN32)
    target_compile_definitions(ascii-chat-rnnoise PRIVATE DLL_EXPORT=1)
endif()
target_include_directories(ascii-chat-rnnoise PRIVATE
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/include
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/rnnoise/src
)
if(NOT WIN32)
    target_link_libraries(ascii-chat-rnnoise PRIVATE m)
endif()

# Preserve non-finite input checks even in Release configurations using fast math.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/processor.c ${CMAKE_CURRENT_SOURCE_DIR}/spectral.c ${CMAKE_CURRENT_SOURCE_DIR}/format.c PROPERTIES COMPILE_OPTIONS "-fno-fast-math")
