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

/** Keymask v2: domain-separated constructed mask from the public-key digest.
 * ASCII and UTF-8 represent the same geometry. No terminal I/O or allocations.
 * This is a recognition aid; exact and perceptual collisions are possible.
 */
asciichat_error_t keymask_render(const uint8_t digest[KEYMASK_DIGEST_SIZE], bool unicode, char *out, size_t size);
