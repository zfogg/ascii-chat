#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Hardware-accelerated CRC32 using ARM instructions (if available)
uint32_t asciichat_crc32_hw(const void *data, size_t len);

// Software fallback implementation
uint32_t asciichat_crc32_sw(const void *data, size_t len);

// Check if hardware CRC32 is available
bool crc32_hw_is_available(void);

// Main CRC32 function - automatically selects best implementation
#define asciichat_crc32(data, len) asciichat_crc32_hw((data), (len))