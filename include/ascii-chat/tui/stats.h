#pragma once
#include <ascii-chat/stats/runtime.h>
#include <libplatform/terminal.h>
terminal_size_t tui_stats_minimum(const stats_view_t *view);
void tui_stats_render(terminal_size_t size, const void *data);
