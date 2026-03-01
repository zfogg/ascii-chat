/**
 * @file debug/backtrace.c
 * @ingroup debug_util
 * @brief 📍 Backtrace capture, symbolization, and formatting implementation
 */

#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/util/string.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

void backtrace_capture(backtrace_t *bt) {
  if (!bt) {
    return;
  }
  bt->count = platform_backtrace(bt->ptrs, 32);
}

void backtrace_symbolize(backtrace_t *bt) {
  if (!bt || bt->tried_symbolize) {
    return; // Already tried or invalid (don't try twice)
  }
  bt->tried_symbolize = true;
  bt->symbols = platform_backtrace_symbols(bt->ptrs, bt->count);
}

void backtrace_capture_and_symbolize(backtrace_t *bt) {
  if (!bt) {
    return;
  }
  backtrace_capture(bt);
  backtrace_symbolize(bt);
}

void backtrace_t_free(backtrace_t *bt) {
  if (!bt) {
    return;
  }
  if (bt->symbols) {
    platform_backtrace_symbols_destroy(bt->symbols);
    bt->symbols = NULL;
  }
}

void backtrace_print(const char *label, const backtrace_t *bt, int skip_frames, int max_frames,
                     backtrace_frame_filter_t filter) {
  if (!bt || !bt->symbols || bt->count <= 0) {
    return;
  }

  // Capture count and symbols once to avoid TOCTOU race with other threads
  int count = bt->count;
  char **symbols = bt->symbols;

  // Calculate frame limits
  int start = skip_frames;
  int end = count;
  if (max_frames > 0 && (start + max_frames) < end) {
    end = start + max_frames;
  }

  // Build backtrace in two versions: colored for terminal, plain for log file
  char colored_buffer[16384] = {0};
  char plain_buffer[16384] = {0};
  int colored_offset = 0;
  int plain_offset = 0;

  // Format log header using logging system's template
  char log_header_buf[512] = {0};
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

  thread_id_t tid = asciichat_thread_self();
  uint64_t tid_val = (uintptr_t)tid;

  // Get current time in nanoseconds for template formatting
  uint64_t time_ns = platform_get_monotonic_time_us() * 1000ULL;

  // Try to format header using the logging system's template with color
  log_template_t *format = log_get_template();
  if (format) {
    // Color the label with WARN color for terminal output
    const char *colored_label_ptr = colored_string(LOG_COLOR_WARN, label);
    char colored_label_buf[256];
    strncpy(colored_label_buf, colored_label_ptr, sizeof(colored_label_buf) - 1);
    colored_label_buf[sizeof(colored_label_buf) - 1] = '\0';

    int len = log_template_apply(format, log_header_buf, sizeof(log_header_buf), LOG_WARN, timestamp, __FILE__,
                                 __LINE__, __func__, tid_val, colored_label_buf, true, time_ns);
    if (len > 0) {
      // Successfully formatted with logging template
      colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                      "%s\n", log_header_buf);
    } else {
      // Fallback: manual formatting if template fails
      safe_snprintf(log_header_buf, sizeof(log_header_buf), "[%s] [WARN] [tid:%llu] %s: %s", timestamp, tid_val,
                    __func__, label);
      const char *colored_header_ptr = colored_string(LOG_COLOR_WARN, log_header_buf);
      char colored_header_buf[512];
      strncpy(colored_header_buf, colored_header_ptr, sizeof(colored_header_buf) - 1);
      colored_header_buf[sizeof(colored_header_buf) - 1] = '\0';
      colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                      "%s\n", colored_header_buf);
    }
  } else {
    // Fallback: manual formatting if no template available
    safe_snprintf(log_header_buf, sizeof(log_header_buf), "[%s] [WARN] [tid:%llu] %s: %s", timestamp, tid_val, __func__,
                  label);
    const char *colored_header_ptr = colored_string(LOG_COLOR_WARN, log_header_buf);
    char colored_header_buf[512];
    strncpy(colored_header_buf, colored_header_ptr, sizeof(colored_header_buf) - 1);
    colored_header_buf[sizeof(colored_header_buf) - 1] = '\0';
    colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                    "%s\n", colored_header_buf);
  }

  // Add plain label header for log file
  plain_offset +=
      safe_snprintf(plain_buffer + plain_offset, sizeof(plain_buffer) - (size_t)plain_offset, "%s\n", label);

  // Build backtrace frames with colored output for terminal, plain for log
  int frame_num = 0;
  for (int i = start; i < end && i < count && colored_offset < (int)sizeof(colored_buffer) - 512; i++) {
    const char *symbol = (symbols && i < count && symbols[i]) ? symbols[i] : "???";

    // Skip frame if filter says to
    if (filter && filter(symbol)) {
      continue;
    }

    // Build frame number string
    char frame_num_str[16];
    safe_snprintf(frame_num_str, sizeof(frame_num_str), "%d", frame_num);

    // Get colored frame number - copy to temp buffer to avoid rotating buffer issues
    const char *colored_num_ptr = colored_string(LOG_COLOR_GREY, frame_num_str);
    char colored_num_buf[256];
    strncpy(colored_num_buf, colored_num_ptr, sizeof(colored_num_buf) - 1);
    colored_num_buf[sizeof(colored_num_buf) - 1] = '\0';

    // Parse symbol to extract parts for selective coloring
    // Format: "[binary_name] function_name() (file:line)"
    char colored_symbol[2048] = {0};
    const char *s = symbol;
    int colored_sym_offset = 0;

    // Color binary name between brackets
    if (*s == '[') {
      colored_sym_offset +=
          safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "[");
      s++;
      // Find closing bracket
      const char *bracket_end = strchr(s, ']');
      if (bracket_end) {
        int bin_len = bracket_end - s;
        char bin_name[512];
        strncpy(bin_name, s, bin_len);
        bin_name[bin_len] = '\0';
        const char *colored_bin_ptr = colored_string(LOG_COLOR_DEBUG, bin_name);
        char colored_bin_buf[512];
        strncpy(colored_bin_buf, colored_bin_ptr, sizeof(colored_bin_buf) - 1);
        colored_bin_buf[sizeof(colored_bin_buf) - 1] = '\0';
        colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                            sizeof(colored_symbol) - colored_sym_offset, "%s", colored_bin_buf);
        colored_sym_offset +=
            safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "] ");
        s = bracket_end + 1;
      }
    }

    // Skip leading spaces after bracket
    while (*s && *s == ' ')
      s++;

    // Parse: could be "function() (file:line)" or "file:line (unresolved)"
    const char *paren_start = strchr(s, '(');

    // Detect format: if there's a colon before the first paren, it's "file:line (description)"
    const char *colon_pos = strchr(s, ':');
    if (colon_pos && paren_start && colon_pos < paren_start) {
      // Format: "file:line (unresolved function)" - rearrange to "(unresolved) (file:line)"
      // Extract file:line part (trim trailing spaces)
      int file_len = paren_start - s;
      while (file_len > 0 && s[file_len - 1] == ' ')
        file_len--;
      char file_part[512];
      strncpy(file_part, s, file_len);
      file_part[file_len] = '\0';

      // Extract description content (without parens)
      const char *paren_end = strchr(paren_start, ')');
      int desc_content_len = paren_end - paren_start - 1; // -1 to skip opening paren
      char desc_content[512];
      strncpy(desc_content, paren_start + 1, desc_content_len); // +1 to skip opening paren
      desc_content[desc_content_len] = '\0';

      const char *colored_desc_ptr = colored_string(LOG_COLOR_ERROR, desc_content);
      char colored_desc_buf[512];
      strncpy(colored_desc_buf, colored_desc_ptr, sizeof(colored_desc_buf) - 1);
      colored_desc_buf[sizeof(colored_desc_buf) - 1] = '\0';
      colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                          sizeof(colored_symbol) - colored_sym_offset, "(%s)", colored_desc_buf);

      // Now color file:line in parens
      // Skip leading spaces in file_part
      const char *file_start = file_part;
      while (*file_start && *file_start == ' ')
        file_start++;

      char *file_colon = strchr(file_start, ':');
      if (file_colon) {
        int filename_len = file_colon - file_start;
        char filename[512];
        strncpy(filename, file_start, filename_len);
        filename[filename_len] = '\0';

        const char *colored_file_ptr = colored_string(LOG_COLOR_DEBUG, filename);
        char colored_file_buf[512];
        strncpy(colored_file_buf, colored_file_ptr, sizeof(colored_file_buf) - 1);
        colored_file_buf[sizeof(colored_file_buf) - 1] = '\0';

        const char *line_num = file_colon + 1;
        const char *colored_line_ptr = colored_string(LOG_COLOR_GREY, line_num);
        char colored_line_buf[512];
        strncpy(colored_line_buf, colored_line_ptr, sizeof(colored_line_buf) - 1);
        colored_line_buf[sizeof(colored_line_buf) - 1] = '\0';

        colored_sym_offset +=
            safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, " (%s:%s)",
                          colored_file_buf, colored_line_buf);
      }
    } else if (paren_start) {
      // Format: "function() (file:line)"
      int func_len = paren_start - s;
      char func_name[512];
      strncpy(func_name, s, func_len);
      func_name[func_len] = '\0';

      const char *colored_func_ptr = colored_string(LOG_COLOR_DEV, func_name);
      char colored_func_buf[512];
      strncpy(colored_func_buf, colored_func_ptr, sizeof(colored_func_buf) - 1);
      colored_func_buf[sizeof(colored_func_buf) - 1] = '\0';
      colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                          sizeof(colored_symbol) - colored_sym_offset, "%s()", colored_func_buf);

      // Find file:line in second set of parens
      s = paren_start + 1;
      while (*s && *s != '(')
        s++;

      if (*s == '(') {
        const char *file_paren_end = strchr(s, ')');
        if (file_paren_end) {
          colored_sym_offset +=
              safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, " (");
          int file_len = file_paren_end - s - 1;
          char file_part[512];
          strncpy(file_part, s + 1, file_len);
          file_part[file_len] = '\0';

          // Skip leading spaces in file_part
          const char *file_start = file_part;
          while (*file_start && *file_start == ' ')
            file_start++;

          char *colon_pos = strchr(file_start, ':');
          if (colon_pos) {
            int filename_len = colon_pos - file_start;
            char filename[512];
            strncpy(filename, file_start, filename_len);
            filename[filename_len] = '\0';

            const char *colored_file_ptr = colored_string(LOG_COLOR_DEBUG, filename);
            char colored_file_buf[512];
            strncpy(colored_file_buf, colored_file_ptr, sizeof(colored_file_buf) - 1);
            colored_file_buf[sizeof(colored_file_buf) - 1] = '\0';

            const char *line_num = colon_pos + 1;
            const char *colored_line_ptr = colored_string(LOG_COLOR_GREY, line_num);
            char colored_line_buf[512];
            strncpy(colored_line_buf, colored_line_ptr, sizeof(colored_line_buf) - 1);
            colored_line_buf[sizeof(colored_line_buf) - 1] = '\0';

            colored_sym_offset +=
                safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, "%s:%s",
                              colored_file_buf, colored_line_buf);
          }
          colored_sym_offset +=
              safe_snprintf(colored_symbol + colored_sym_offset, sizeof(colored_symbol) - colored_sym_offset, ")");
        }
      }
    } else {
      // No parens, likely a hex address - color with FATAL
      const char *colored_addr_ptr = colored_string(LOG_COLOR_FATAL, s);
      char colored_addr_buf[512];
      strncpy(colored_addr_buf, colored_addr_ptr, sizeof(colored_addr_buf) - 1);
      colored_addr_buf[sizeof(colored_addr_buf) - 1] = '\0';
      colored_sym_offset += safe_snprintf(colored_symbol + colored_sym_offset,
                                          sizeof(colored_symbol) - colored_sym_offset, "%s", colored_addr_buf);
    }

    // Format colored buffer: "  [colored_num] colored_symbol\n"
    colored_offset += safe_snprintf(colored_buffer + colored_offset, sizeof(colored_buffer) - (size_t)colored_offset,
                                    "  [%s] %s\n", colored_num_buf, colored_symbol);

    // Format plain buffer: "  [num] symbol\n"
    plain_offset += safe_snprintf(plain_buffer + plain_offset, sizeof(plain_buffer) - (size_t)plain_offset,
                                  "  [%d] %s\n", frame_num, symbol);
    frame_num++;
  }

  // Output colored to stderr and plain to log file
  fprintf(stderr, "%s", colored_buffer);
  log_file_msg("%s", plain_buffer);
}

void backtrace_print_many(const char *label, const backtrace_t *bts, int count) {
  if (!bts || count <= 0) {
    return;
  }
  for (int i = 0; i < count; i++) {
    backtrace_print(label, &bts[i], 0, 0, NULL);
  }
}

int backtrace_format(char *buf, size_t buf_size, const char *label, const backtrace_t *bt, int skip_frames,
                     int max_frames, backtrace_frame_filter_t filter) {
  if (!buf || buf_size == 0 || !bt || !bt->symbols || bt->count <= 0) {
    return -1;
  }

  // Capture count and symbols once to avoid TOCTOU race with other threads
  int count = bt->count;
  char **symbols = bt->symbols;

  int offset = 0;

  // Add label
  offset += safe_snprintf(buf + offset, buf_size - (size_t)offset, "%s\n", label);

  // Calculate frame limits
  int start = skip_frames;
  int end = count;
  if (max_frames > 0 && (start + max_frames) < end) {
    end = start + max_frames;
  }

  // Add frames
  int frame_num = 0;
  for (int i = start; i < end && i < count && offset < (int)buf_size - 128; i++) {
    const char *symbol = (symbols && i < count && symbols[i]) ? symbols[i] : "???";

    // Skip frame if filter says to
    if (filter && filter(symbol)) {
      continue;
    }

    offset += safe_snprintf(buf + offset, buf_size - (size_t)offset, "  [%d] %s\n", frame_num, symbol);
    frame_num++;
  }

  return offset;
}
