#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEYMASK_DIGEST_SIZE 32
#define KEYMASK_WIDTH 34
#define KEYMASK_ROWS 18
// Includes borders, newlines, UTF-8 blocks, and the terminating NUL.
#define KEYMASK_BUFFER_SIZE 1655

/** SHA-256 of the 32 public-key bytes, independent of key source or encoding. */
asciichat_error_t key_fingerprint_digest(const uint8_t public_key[32], uint8_t digest[KEYMASK_DIGEST_SIZE]);

/** Keymask v1: row-major, MSB-first 16x16 digest bitmap, mirrored to 32x16.
 * ASCII and UTF-8 represent the same bits. No terminal I/O or allocations.
 * Different digests have different exact images, not necessarily distinct-looking images.
 */
asciichat_error_t keymask_render(const uint8_t digest[KEYMASK_DIGEST_SIZE], bool unicode, char *out, size_t size);
