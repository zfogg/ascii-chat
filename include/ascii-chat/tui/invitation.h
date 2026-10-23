#pragma once

#include <stdbool.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/platform/terminal.h>

/** Render the discovery invitation into a frame without writing to the terminal.
 * An empty string shows creation progress; joining shows connection progress.
 * Text is plain ASCII, and the Run instruction immediately follows the string.
 */
void invitation_render(frame_buffer_t *buf, terminal_size_t size, const char *session_string, bool joining,
                       int animation_frame, bool use_colors);

/** Shared rainbow logo used by the startup splash and discovery invitation. */
void invitation_render_logo(frame_buffer_t *buf, int cols, int animation_frame, bool use_colors, bool compact);
