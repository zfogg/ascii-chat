/**
 * @file formatter.c
 * @brief ascii-chat compatibility wrappers around generic libflags groff formatting.
 */

#include <ascii-chat/options/manpage/formatter.h>
#include <libflags/manpage/formatter.h>

const char *manpage_fmt_escape_groff(const char *str) {
  return flags_manpage_escape_groff(str);
}

void manpage_fmt_write_section(FILE *f, const char *section_name) {
  flags_manpage_write_section(f, section_name);
}

void manpage_fmt_write_blank_line(FILE *f) {
  flags_manpage_write_blank_line(f);
}

void manpage_fmt_write_bold(FILE *f, const char *text) {
  flags_manpage_write_bold(f, text);
}

void manpage_fmt_write_italic(FILE *f, const char *text) {
  flags_manpage_write_italic(f, text);
}

void manpage_fmt_write_tagged_paragraph(FILE *f) {
  flags_manpage_write_tagged_paragraph(f);
}

void manpage_fmt_write_text(FILE *f, const char *text) {
  flags_manpage_write_text(f, text);
}

void manpage_fmt_write_title(FILE *f, const char *program_name, const char *mode_name, const char *brief_description) {
  flags_manpage_write_title(f, program_name, mode_name, brief_description);
}
