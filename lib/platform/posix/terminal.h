#pragma once

// Serialize terminal queries against foreground process-group handoffs.
void terminal_input_lock(void);
void terminal_input_unlock(void);
