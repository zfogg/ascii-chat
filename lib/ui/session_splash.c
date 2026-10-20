#include <ascii-chat/ui/session.h>
#include <ascii-chat/ui/invitation.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/string.h>
#include <stdio.h>
#include <string.h>

void ui_session_splash_render(frame_buffer_t *buf, terminal_size_t term_size, void *user_data) {
  const ui_splash_view_t *ctx = (const ui_splash_view_t *)user_data;
  if (!ctx) {
    return;
  }

  if (ctx->discovery.enabled) {
    invitation_render(buf, term_size, ctx->discovery.session_string, ctx->discovery.joining,
                      (int)(ctx->animation.seconds * 60), ctx->use_colors);
    return;
  }

  const char *tagline = "Video chat in your terminal";
  frame_buffer_render_border(buf, term_size.cols, log_level_color(LOG_COLOR_DEBUG));
  invitation_render_logo(buf, term_size.cols - 1, (int)(ctx->animation.seconds * 60), ctx->use_colors, false);

  // Line 6: Tagline (centered, truncated if too long)
  char plain_tagline[512];
  int tagline_len = (int)strlen(tagline);
  int tagline_pad = (term_size.cols - tagline_len) / 2;
  if (tagline_pad < 0) {
    tagline_pad = 0;
  }

  int tpos = 0;
  for (int j = 0; j < tagline_pad && tpos < (int)sizeof(plain_tagline) - 1; j++) {
    plain_tagline[tpos++] = ' ';
  }
  snprintf(plain_tagline + tpos, sizeof(plain_tagline) - tpos, "%s", tagline);

  // Check visible width and truncate if needed
  int tagline_visible_width = display_width(plain_tagline);
  if (tagline_visible_width < 0) {
    tagline_visible_width = (int)strlen(plain_tagline);
  }
  if (term_size.cols > 0 && tagline_visible_width >= term_size.cols) {
    plain_tagline[term_size.cols - 1] = '\0';
  }

  frame_buffer_printf(buf, "%s\n", plain_tagline);

  // Line 7: Update notification (if available)
  if (ctx->update_notification[0] != '\0') {
    char plain_update[1024];
    int update_len = (int)strlen(ctx->update_notification);
    int update_pad = (term_size.cols - update_len) / 2;
    if (update_pad < 0) {
      update_pad = 0;
    }

    int upos = 0;
    for (int j = 0; j < update_pad && upos < (int)sizeof(plain_update) - 1; j++) {
      plain_update[upos++] = ' ';
    }
    snprintf(plain_update + upos, sizeof(plain_update) - upos, "%s", ctx->update_notification);

    // Check visible width and truncate if needed
    int update_visible_width = display_width(plain_update);
    if (update_visible_width < 0) {
      update_visible_width = (int)strlen(plain_update);
    }
    if (term_size.cols > 0 && update_visible_width >= term_size.cols) {
      plain_update[term_size.cols - 1] = '\0';
    }

    // Print in yellow/warning color
    frame_buffer_printf(buf, "%s\n", colored_string(LOG_COLOR_WARN, plain_update));
  }

  // Line 8 (or 7 if no update): Connection target (centered, truncated if too long)
  const char *connection_target = ctx->connection_target;

  char plain_connection[512];
  int connection_len = (int)strlen(connection_target);
  int connection_pad = (term_size.cols - connection_len) / 2;
  if (connection_pad < 0) {
    connection_pad = 0;
  }

  int cpos = 0;
  for (int j = 0; j < connection_pad && cpos < (int)sizeof(plain_connection) - 1; j++) {
    plain_connection[cpos++] = ' ';
  }
  snprintf(plain_connection + cpos, sizeof(plain_connection) - cpos, "%s", connection_target);

  // Check visible width and truncate if needed
  int connection_visible_width = display_width(plain_connection);
  if (connection_visible_width < 0) {
    connection_visible_width = (int)strlen(plain_connection);
  }
  if (term_size.cols > 0 && connection_visible_width >= term_size.cols) {
    plain_connection[term_size.cols - 1] = '\0';
  }

  frame_buffer_printf(buf, "%s\n", plain_connection);

  // Line 9 (or 8 if no update): Bottom border
  frame_buffer_render_border(buf, term_size.cols, log_level_color(LOG_COLOR_DEBUG));
}
