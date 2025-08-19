#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Hardware-accelerated CRC32 computation
// Automatically dispatches to hardware implementation when available,
// falls back to software implementation otherwise.
uint32_t asciichat_crc32_hw(const void *data, size_t len);

// Software-only CRC32 implementation (always available)
uint32_t asciichat_crc32_sw(const void *data, size_t len);

// Check if hardware CRC32 acceleration is available at runtime
bool crc32_hw_is_available(void);

// Main CRC32 dispatcher macro - use this in application code
#define asciichat_crc32(data, len) asciichat_crc32_hw((data), (len))