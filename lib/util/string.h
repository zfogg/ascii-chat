#pragma once

#include <stddef.h>

void escape_ascii(const char *str, const char *escape_char, char *out_buffer, size_t out_buffer_size);
