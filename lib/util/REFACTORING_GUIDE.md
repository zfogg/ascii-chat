# lib/util/ Refactoring Guide

This document describes new utility functions added to `lib/util/` to consolidate duplicate code patterns across the codebase.

## Overview

Three new utility headers were added to centralize common code patterns that appeared in 5-12 different files:

1. **lib/util/image_size.h** - Safe overflow-checked image/buffer size calculations
2. **lib/util/int_parse.h** - Safe integer parsing with range validation
3. **lib/util/endian.h** - Network byte order conversion helpers

## Motivation

Before refactoring, the codebase had significant code duplication:

- **13+ occurrences** of dimension-based buffer size calculations with manual overflow checking
- **25+ occurrences** of strtol/strtoul parsing code with ad-hoc error handling
- **73 occurrences** of network byte order conversions (htons/ntohl) scattered throughout

These utilities consolidate these patterns into reusable, tested functions.

---

## lib/util/image_size.h

### Purpose
Safe calculation of image buffer sizes with proper overflow detection.

### Functions

#### `image_calc_pixel_count(width, height, out_pixel_count)`
```c
// BEFORE (found in 5+ files):
if (height > ULONG_MAX / width) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Image dimensions too large: %zu x %zu", width, height);
    return NULL;
}
size_t pixel_count = width * height;

// AFTER (consolidated):
size_t pixel_count;
if (image_calc_pixel_count(width, height, &pixel_count) != ASCIICHAT_OK) {
    return NULL;  // Error already set by function
}
```

#### `image_calc_rgb_size(width, height, out_size)`
```c
// BEFORE (found in 3+ files):
size_t pixel_count = width * height;
if (pixel_count > SIZE_MAX / 3) {
    SET_ERRNO(ERROR_INVALID_PARAM, "RGB buffer would overflow");
    return NULL;
}
size_t rgb_size = pixel_count * 3;

// AFTER:
size_t rgb_size;
if (image_calc_rgb_size(width, height, &rgb_size) != ASCIICHAT_OK) {
    return NULL;
}
```

#### `image_validate_dimensions(width, height)`
```c
// BEFORE (scattered throughout):
if (width == 0 || height == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Dimensions must be non-zero");
}
if (width > IMAGE_MAX_WIDTH || height > IMAGE_MAX_HEIGHT) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Dimensions exceed maximum");
}

// AFTER:
if (image_validate_dimensions(width, height) != ASCIICHAT_OK) {
    return ASCIICHAT_ERROR;  // errno already set
}
```

### Files Using image_size.h
These files should be refactored to use image_size.h:
- `lib/video/image.c` - Image allocation and validation (already partially refactored)
- `lib/video/simd/ascii_simd.c` - SIMD processing buffer setup
- `lib/video/simd/avx2.c` - AVX2 buffer calculations
- `lib/video/webcam/linux/webcam_v4l2.c` - Video capture buffer setup
- `src/server/stream.c` - Network stream buffer setup

---

## lib/util/int_parse.h

### Purpose
Safe integer parsing from strings with consistent error handling and range validation.

### Functions

#### `int_parse_long(str, out_value, min_value, max_value)`
```c
// BEFORE (found in 6+ files):
char *endptr;
long port = strtol(port_str, &endptr, 10);
if (port <= 0 || port > 65535) {
    log_error("Invalid port: %s", port_str);
    return ERROR_INVALID_PARAM;
}

// AFTER:
long port;
if (int_parse_long(port_str, &port, 1, 65535) != ASCIICHAT_OK) {
    return ASCIICHAT_ERROR;  // Error already logged with context
}
```

#### `int_parse_port(str, out_port)`
```c
// BEFORE:
long port_long = strtol(port_str, NULL, 10);
if (port_long <= 0 || port_long > 65535) {
    return -1;
}
*port_output = (uint16_t)port_long;

// AFTER:
if (int_parse_port(port_str, port_output) != ASCIICHAT_OK) {
    return -1;
}
```

#### `int_parse_uint32(str, out_value, min_value, max_value)`
```c
// BEFORE (in options.c):
int val = strtoint_safe(value_str);
if (val == INT_MIN) {
    return ERROR_INVALID_PARAM;
}

// AFTER:
uint32_t val;
if (int_parse_uint32(value_str, &val, 0, 100) != ASCIICHAT_OK) {
    return ASCIICHAT_ERROR;
}
```

### Files Using int_parse.h
These files should be refactored to use int_parse.h:
- `lib/util/ip.c` - Port number parsing (✅ DONE)
- `lib/options.c` - CLI argument parsing
- `lib/crypto/keys/keys.c` - Key parameter parsing
- `src/server/main.c` - Server option parsing
- `src/client/main.c` - Client option parsing

---

## lib/util/endian.h

### Purpose
Type-safe helpers for network byte order conversions. Provides both direct conversions and buffer read/write operations.

### Functions

#### Direct Conversions
```c
// BEFORE (found throughout lib/network/):
uint32_t network_magic = htonl(header->magic);
uint16_t network_type = htons(header->type);

// AFTER:
uint32_t network_magic = endian_pack_u32(header->magic);
uint16_t network_type = endian_pack_u16(header->type);
```

#### Buffer Operations
```c
// BEFORE (unsafe - assumes proper alignment):
uint32_t *ptr = (uint32_t *)buffer;
*ptr = htonl(value);  // Potential alignment issues!

// AFTER (safe - handles alignment):
endian_write_u32(buffer, value);  // Properly handles any alignment
uint32_t result = endian_read_u32(buffer);
```

### Example: Refactoring Packet Code
```c
// BEFORE (from lib/network/packet.c line 86-89):
uint32_t magic = ntohl(header->magic);
uint16_t type = ntohs(header->type);
uint32_t len = ntohl(pkt_len_network);
uint32_t crc = ntohl(header->crc32);

// AFTER:
uint32_t magic = endian_unpack_u32(header->magic);
uint16_t type = endian_unpack_u16(header->type);
uint32_t len = endian_unpack_u32(pkt_len_network);
uint32_t crc = endian_unpack_u32(header->crc32);
```

### Files Using endian.h
These files should be refactored to use endian.h:
- `lib/network/packet.c` - All packet header conversions (73+ occurrences)
- `lib/network/av.c` - Audio/video metadata conversions
- `lib/network/packet_queue.c` - Queue packet handling
- `lib/crypto/crypto.c` - Encrypted data handling

---

## Refactoring Checklist

### Priority 1 (High Impact)
- [ ] `lib/options.c` - Replace all strtol calls with int_parse_*()
  - Impact: Consolidates 6+ integer parsing patterns
  - Lines: 81, 136, 454, 1450, 1489, 1521

- [ ] `lib/network/packet.c` - Replace htons/ntohl with endian_*()
  - Impact: 19+ network byte order conversions
  - Benefit: More readable code with consistent error handling

### Priority 2 (Medium Impact)
- [ ] `lib/video/simd/ascii_simd.c` - Use image_calc_*() helpers
  - Impact: 5+ dimension calculations
  - Benefit: Consistent overflow checking

- [ ] `src/server/stream.c` - Use image_calc_*() for buffer setup
  - Impact: 3+ dimension calculations

### Priority 3 (Low Impact - Edge Cases)
- [ ] `lib/util/parsing.c` - Review SIZE/AUDIO message parsing
- [ ] `lib/crypto/keys/keys.c` - Check for integer parsing patterns
- [ ] `lib/video/webcam/linux/webcam_v4l2.c` - Buffer calculations

---

## Design Principles

### Error Handling
All utility functions follow the same pattern:
1. Return `asciichat_error_t` error code
2. Set errno with detailed context using `SET_ERRNO()`
3. Set output parameters only on success
4. NULL-check all pointers

### Memory Safety
- No malloc/free - all functions operate on provided buffers
- Overflow checking before every multiplication
- Range validation before type conversions

### Consistency
- All functions use `size_t` for sizes (not `int` or `long`)
- All port numbers use `uint16_t` (0-65535 range)
- All error codes use `asciichat_error_t`

---

## Testing

New utilities should be tested in `tests/unit/`:
- Test buffer size calculations with various dimensions
- Test integer parsing with edge cases (0, MAX_INT, overflow)
- Test endian conversion with both big and little-endian values
- Test refactored code maintains same behavior as original

---

## Future Improvements

1. **String utilities** - Consolidate string operations in lib/util/string.h
2. **Buffer operations** - Create lib/util/buffer.h for read/write operations
3. **Validation** - Expand lib/util/validation.h with more check macros
4. **Type safety** - Consider creating type-checked wrappers for common operations

---

## References

- lib/util/image_size.h - Safe image size calculations
- lib/util/int_parse.h - Safe integer parsing
- lib/util/endian.h - Network byte order helpers
- lib/common.h - Core macros (SET_ERRNO, SAFE_MALLOC, etc.)
- lib/util/validation.h - Existing validation macros
