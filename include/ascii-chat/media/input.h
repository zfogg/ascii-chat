#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>

/** Normalize one dropped/pasted path or URL without interpreting shell commands. */
asciichat_error_t media_input_normalize(const char *text, char *output, size_t capacity);
