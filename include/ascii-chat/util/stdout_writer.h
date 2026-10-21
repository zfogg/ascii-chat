#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>
#include <stdio.h>

/** Write exactly size bytes, including embedded NULs, without closing stream. */
asciichat_error_t stdout_writer_write(FILE *stream, const void *data, size_t size);
