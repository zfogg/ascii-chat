#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Generate a bounded, repeating 20-second mono diagnostic phrase.
 * Position is in samples at sample_rate; arbitrary block sizes are equivalent.
 * No allocations or mutable global state. Peak amplitude is at most 0.18.
 */
void audio_test_pattern(float *output, size_t count, uint64_t position, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif
