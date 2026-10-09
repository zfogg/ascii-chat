#pragma once
#include <ascii-chat/asciichat_errno.h>

// Main-thread-only OS naming surfaces beyond the argv command line.
asciichat_error_t platform_process_title_native_set(const char *title, const char *short_name);
