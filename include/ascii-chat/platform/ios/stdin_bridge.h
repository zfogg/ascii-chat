#pragma once

#include <stddef.h>
#include <sys/types.h>

ssize_t ios_stdin_bridge_read(void *buffer, size_t count);
ssize_t ios_stdin_bridge_push(const void *data, size_t count);
void ios_stdin_bridge_reset(void);
