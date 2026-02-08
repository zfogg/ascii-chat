/**
 * @file platform/wasm/stubs/portaudio.h
 * @brief Stub PortAudio header for WASM builds
 * @ingroup platform
 *
 * WASM builds don't use PortAudio - audio is handled by Web Audio API.
 * This stub provides minimal type definitions so audio.h can compile.
 */

#pragma once

// Stub PortAudio types (not actually used in WASM)
typedef void PaStream;
typedef int PaError;
typedef unsigned long PaStreamCallbackFlags;
typedef struct PaStreamCallbackTimeInfo PaStreamCallbackTimeInfo;

// Stub error codes
#define paNoError 0
#define paNotInitialized -10000

// Stub functions (not implemented for WASM)
static inline PaError Pa_Initialize(void) {
  return paNotInitialized;
}
static inline PaError Pa_Terminate(void) {
  return paNotInitialized;
}
