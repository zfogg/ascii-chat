#pragma once
#include <stdbool.h>

// Serialize terminal queries against foreground process-group handoffs.
void terminal_input_lock(void);
void terminal_input_unlock(void);
bool terminal_release_subprocess_gate(int fd);
