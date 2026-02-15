/**
 * @file formatter.c
 * @brief Groff/troff formatting utilities for man page generation
 * @ingroup options_manpage
 *
 * Provides utilities for generating properly formatted groff/troff output
 * for man pages.
 */

#include <ascii-chat/options/manpage/formatter.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/system.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// ============================================================================
// Escape and Basic Formatting
// ============================================================================

const char *manpage_fmt_escape_groff(const char *str) {
  // For simplicity, we'll just return the string as-is
  // In a more robust implementation, we'd escape special characters
  // But for man page content, most strings don't have problematic characters
  return str ? str : "";
}

void manpage_fmt_write_section(FILE *f, const char *section_name) {
  if (!f || !section_name) {
    return;
  }
  fprintf(f, ".SH %s\n", section_name);
}

void manpage_fmt_write_blank_line(FILE *f) {
  if (!f) {
    return;
  }
  fprintf(f, "\n");
}

void manpage_fmt_write_bold(FILE *f, const char *text) {
  if (!f || !text) {
    return;
  }
  fprintf(f, ".B %s\n", text);
}

void manpage_fmt_write_italic(FILE *f, const char *text) {
  if (!f || !text) {
    return;
  }
  fprintf(f, ".I %s\n", text);
}

void manpage_fmt_write_tagged_paragraph(FILE *f) {
  if (!f) {
    return;
  }
  fprintf(f, ".TP\n");
}

void manpage_fmt_write_text(FILE *f, const char *text) {
  if (!f) {
    return;
  }

  if (text) {
    fprintf(f, "%s\n", text);
  } else {
    fprintf(f, "\n");
  }
}

void manpage_fmt_write_title(FILE *f, const char *program_name, const char *mode_name, const char *brief_description) {
  if (!f || !program_name || !brief_description) {
    return;
  }

  time_t now = time(NULL);
  struct tm tm_buf;
  platform_localtime(&now, &tm_buf);
  char date_str[32];
  strftime(date_str, sizeof(date_str), "%B %Y", &tm_buf);

  // Build full program name (e.g., "ascii-chat-server" or just "ascii-chat")
  char full_name[256];
  if (mode_name) {
    safe_snprintf(full_name, sizeof(full_name), "%s-%s", program_name, mode_name);
  } else {
    safe_snprintf(full_name, sizeof(full_name), "%s", program_name);
  }

  // .TH NAME SECTION DATE SOURCE MANUAL
  // Section 1 = user commands, 5 = file formats
  fprintf(f, ".TH %s 1 \"%s\" \"%s\" \"User Commands\"\n", full_name, date_str, program_name);
  fprintf(f, ".SH NAME\n");
  fprintf(f, ".B %s\n", full_name);
  fprintf(f, "\\- %s\n", manpage_fmt_escape_groff(brief_description));
  fprintf(f, "\n");
}
