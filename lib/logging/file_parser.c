/**
 * @file logging/file_parser.c
 * @brief Log file parsing and tailing implementation
 */

#include "ascii-chat/common/error_codes.h"
#include <ascii-chat/logging/file_parser.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/log/colorize.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// Parse a single log line in format: [TIMESTAMP] [LEVEL] [tid:THREAD_ID] FILE:LINE in FUNC(): MESSAGE
// Example: [2026-02-08 12:34:56.789] [DEBUG] [tid:12345] src/main.c:42 in main(): Server started
// Preserves full log line format for proper recoloring in interactive grep
bool log_file_parser_parse_line(const char *line, session_log_entry_t *out_entry) {
  if (!line || !out_entry || !*line) {
    return false;
  }

  memset(out_entry, 0, sizeof(*out_entry));

  // Skip empty lines and whitespace-only lines
  if (strspn(line, " \t\r\n") == strlen(line)) {
    return false;
  }

  size_t line_len = strlen(line);
  if (line_len == 0) {
    return false;
  }

  // Check for opening bracket (start of timestamp)
  if (line[0] != '[') {
    return false;
  }

  // Find closing bracket of timestamp
  const char *bracket1 = strchr(line, ']');
  if (!bracket1) {
    return false;
  }

  // Find opening bracket of level (should be right after closing bracket + space)
  if (bracket1[1] != ' ' || bracket1[2] != '[') {
    return false;
  }

  // Find closing bracket of level
  const char *bracket2 = strchr(bracket1 + 2, ']');
  if (!bracket2) {
    return false;
  }

  // We have a valid log line - preserve the entire line as-is for recoloring
  // Copy full line to output, always bounded to prevent overflow
  strncpy(out_entry->message, line, SESSION_LOG_LINE_MAX - 1);
  out_entry->message[SESSION_LOG_LINE_MAX - 1] = '\0';

  // Remove trailing newline if present
  size_t msg_len = strlen(out_entry->message);
  if (msg_len > 0 && out_entry->message[msg_len - 1] == '\n') {
    out_entry->message[msg_len - 1] = '\0';
  }

  out_entry->sequence = 0; // Will be filled in by merge function

  return true;
}

// Tail a log file by reading last max_size bytes and parsing entries
size_t log_file_parser_tail(const char *file_path, size_t max_size, session_log_entry_t **out_entries,
                            size_t max_count) {
  if (!file_path || !out_entries || !max_count) {
    return 0;
  }

  // Allocate output array
  *out_entries = SAFE_MALLOC(max_count * sizeof(session_log_entry_t), session_log_entry_t *);
  if (!*out_entries) {
    return 0;
  }

  // Open file
  FILE *fp = fopen(file_path, "r");
  if (!fp) {
    SET_ERRNO(ERROR_FILE_OPERATION, "Cannot open log file for tailing: %s (errno: %s)", file_path,
              SAFE_STRERROR(errno));
    SAFE_FREE(*out_entries);
    return 0;
  }

  // Get file size
  if (fseek(fp, 0, SEEK_END) != 0) {
    SET_ERRNO(ERROR_FILE_OPERATION, "Cannot seek to end of log file: %s (errno: %s)", file_path, SAFE_STRERROR(errno));
    fclose(fp);
    SAFE_FREE(*out_entries);
    return 0;
  }

  long file_size = ftell(fp);
  if (file_size <= 0) {
    SET_ERRNO(ERROR_FILE_OPERATION, "Invalid log file size for: %s", file_path);
    fclose(fp);
    SAFE_FREE(*out_entries);
    return 0;
  }

  // Seek to start of tail region
  size_t tail_size = (size_t)file_size > max_size ? max_size : (size_t)file_size;
  long seek_pos = file_size - (long)tail_size;
  if (fseek(fp, seek_pos, SEEK_SET) != 0) {
    SET_ERRNO(ERROR_FILE_OPERATION, "Cannot seek to tail position in log file: %s (errno: %s)", file_path,
              SAFE_STRERROR(errno));
    fclose(fp);
    SAFE_FREE(*out_entries);
    return 0;
  }

  // Read tail into buffer
  uint8_t *tail_buffer = SAFE_MALLOC(tail_size + 1, uint8_t *);
  if (!tail_buffer) {
    fclose(fp);
    SAFE_FREE(*out_entries);
    return 0;
  }

  size_t bytes_read = fread(tail_buffer, 1, tail_size, fp);
  fclose(fp);
  if (bytes_read == 0) {
    SAFE_FREE(tail_buffer);
    SAFE_FREE(*out_entries);
    return 0;
  }

  tail_buffer[bytes_read] = '\0';

  // Parse lines from tail buffer
  // If we started in the middle of a line, skip to the first complete line
  char *line_start = (char *)tail_buffer;
  if (seek_pos > 0) {
    // Skip partial line at start
    char *first_newline = strchr(line_start, '\n');
    if (first_newline) {
      line_start = first_newline + 1;
    } else {
      // Only one partial line, skip it
      SAFE_FREE(tail_buffer);
      SAFE_FREE(*out_entries);
      return 0;
    }
  }

  // Parse complete lines
  size_t entry_count = 0;
  char *current = line_start;
  while (*current && entry_count < max_count) {
    // Find end of line
    char *line_end = strchr(current, '\n');
    if (!line_end) {
      line_end = current + strlen(current);
    }

    // Null-terminate the line temporarily
    char saved_char = *line_end;
    *line_end = '\0';

    // Parse the line
    if (log_file_parser_parse_line(current, &(*out_entries)[entry_count])) {
      entry_count++;
    }

    // Restore the character
    *line_end = saved_char;

    // Move to next line
    if (saved_char == '\n') {
      current = line_end + 1;
    } else {
      break; // End of buffer
    }
  }

  SAFE_FREE(tail_buffer);
  return entry_count;
}

// Compare entries for sorting by sequence (or message if sequence is equal)
static int compare_entries(const void *a, const void *b) {
  const session_log_entry_t *entry_a = (const session_log_entry_t *)a;
  const session_log_entry_t *entry_b = (const session_log_entry_t *)b;

  if (entry_a->sequence != entry_b->sequence) {
    return entry_a->sequence < entry_b->sequence ? -1 : 1;
  }

  // If sequences are equal, compare messages
  return strcmp(entry_a->message, entry_b->message);
}

/**
 * Extract timestamp from a log entry for deduplication.
 * Expected format: [YYYY-MM-DD HH:MM:SS.mmm]
 * Returns pointer to timestamp string within the message, or NULL if not found.
 */
static const char *extract_timestamp_from_message(const char *message) {
  if (!message || message[0] != '[') {
    return NULL;
  }
  // Point to the timestamp (after the opening bracket)
  return message + 1;
}

// Merge and deduplicate entries from two sources
size_t log_file_parser_merge_and_dedupe(const session_log_entry_t *buffer_entries, size_t buffer_count,
                                        const session_log_entry_t *file_entries, size_t file_count,
                                        session_log_entry_t **out_merged) {
  if (!out_merged) {
    return 0;
  }

  // Allocate space for all entries (plus extra for recolored versions)
  size_t total_count = buffer_count + file_count;
  if (total_count == 0) {
    *out_merged = NULL;
    return 0;
  }

  session_log_entry_t *merged = SAFE_MALLOC(total_count * sizeof(session_log_entry_t), session_log_entry_t *);
  if (!merged) {
    *out_merged = NULL;
    return 0;
  }

  // Copy buffer entries (already colored)
  if (buffer_count > 0) {
    memcpy(merged, buffer_entries, buffer_count * sizeof(session_log_entry_t));
  }

  // Recolor and copy file entries
  // File entries are plain text from disk: [TIMESTAMP] [LEVEL] [tid:...] FILE:LINE in FUNC(): MESSAGE
  // We must recolor them with ANSI codes for terminal display AND syntax-highlight the message content
  static int recolor_attempts = 0;
  static int recolor_successes = 0;
  for (size_t i = 0; i < file_count; i++) {
    char colored[SESSION_LOG_LINE_MAX];
    size_t colored_len = log_recolor_plain_entry(file_entries[i].message, colored, sizeof(colored));
    recolor_attempts++;

    if (colored_len > 0) {
      // Successfully recolored header - now apply message syntax highlighting
      // Extract the plain message part for syntax highlighting
      const char *plain_msg = file_entries[i].message;
      const char *msg_marker = strstr(plain_msg, "(): ");
      if (msg_marker) {
        const char *plain_message = msg_marker + 4;

        // Syntax-highlight just the message part
        const char *highlighted_msg = colorize_log_message(plain_message);
        if (highlighted_msg && *highlighted_msg) {
          // Reconstruct the full line combining colored header + highlighted message
          // We need to find the "(): " part in the COLORED output and reconstruct from there
          // Since the colored output contains ANSI codes, we need to skip past them to find the marker

          char final_line[SESSION_LOG_LINE_MAX];
          size_t final_len = 0;

          // Copy colored output up to and including "(): "
          // We scan through the colored output looking for the literal sequence "(): "
          const char *p = colored;
          int paren_count = 0;
          while (*p && final_len < SESSION_LOG_LINE_MAX - 1) {
            final_line[final_len++] = *p;

            // Track when we see "(): "
            if (*p == '(' && *(p + 1) == ')' && *(p + 2) == ':' && *(p + 3) == ' ') {
              // Copy the ": " part
              final_line[final_len++] = *(++p); // ')'
              final_line[final_len++] = *(++p); // ':'
              final_line[final_len++] = *(++p); // ' '
              p++;                              // Move past the space
              break;                            // Stop here, append highlighted message
            }
            p++;
          }

          // Append the highlighted message
          const char *msg_ptr = highlighted_msg;
          while (*msg_ptr && final_len < SESSION_LOG_LINE_MAX - 1) {
            final_line[final_len++] = *msg_ptr++;
          }
          final_line[final_len] = '\0';

          strncpy(merged[buffer_count + i].message, final_line, SESSION_LOG_LINE_MAX - 1);
          merged[buffer_count + i].message[SESSION_LOG_LINE_MAX - 1] = '\0';
          recolor_successes++;
        } else {
          // Highlighting returned nothing, use colored version as-is
          strncpy(merged[buffer_count + i].message, colored, SESSION_LOG_LINE_MAX - 1);
          merged[buffer_count + i].message[SESSION_LOG_LINE_MAX - 1] = '\0';
          recolor_successes++;
        }
      } else {
        // No message marker found, use colored version
        strncpy(merged[buffer_count + i].message, colored, SESSION_LOG_LINE_MAX - 1);
        merged[buffer_count + i].message[SESSION_LOG_LINE_MAX - 1] = '\0';
        recolor_successes++;
      }
    } else {
      // Recoloring failed - keep original plain text
      strncpy(merged[buffer_count + i].message, file_entries[i].message, SESSION_LOG_LINE_MAX - 1);
      merged[buffer_count + i].message[SESSION_LOG_LINE_MAX - 1] = '\0';

      // Log failures to help debug format issues
      static int failure_count = 0;
      if (failure_count < 10) {
        // Show hex dump of first 20 bytes to identify format issues
        const unsigned char *bytes = (const unsigned char *)file_entries[i].message;
        log_debug("Recolor failed [%d]: starts with: %c%c%c%c (0x%02x%02x%02x%02x) %.80s", failure_count + 1,
                  bytes[0] >= 32 && bytes[0] < 127 ? bytes[0] : '?', bytes[1] >= 32 && bytes[1] < 127 ? bytes[1] : '?',
                  bytes[2] >= 32 && bytes[2] < 127 ? bytes[2] : '?', bytes[3] >= 32 && bytes[3] < 127 ? bytes[3] : '?',
                  bytes[0], bytes[1], bytes[2], bytes[3], file_entries[i].message);
        failure_count++;
      }
    }
    merged[buffer_count + i].sequence = file_entries[i].sequence;
  }
  if (file_count > 0) {
    log_debug("File log recoloring: %d/%d entries successfully recolored", recolor_successes, recolor_attempts);
  }

  // Assign sequence numbers: file entries are older (lower seq), buffer entries newer (higher seq)
  uint64_t next_seq = 1;
  for (size_t i = 0; i < file_count; i++) {
    if (merged[i].sequence == 0) {
      merged[i].sequence = next_seq++;
    } else {
      next_seq = merged[i].sequence + 1;
    }
  }
  for (size_t i = file_count; i < total_count; i++) {
    if (merged[i].sequence == 0) {
      merged[i].sequence = next_seq++;
    } else {
      next_seq = merged[i].sequence + 1;
    }
  }

  // Sort by sequence
  qsort(merged, total_count, sizeof(session_log_entry_t), compare_entries);

  // Deduplicate by timestamp extraction: if two logs have the same timestamp,
  // consider them duplicates (buffer entry already colored, file entry now colored)
  size_t output_idx = 0;
  for (size_t i = 0; i < total_count; i++) {
    // Skip if this message is the same as the previous one (exact match)
    if (i > 0 && strcmp(merged[i].message, merged[output_idx - 1].message) == 0) {
      continue; // Exact duplicate, skip
    }

    // Also skip if timestamps match (different sources of same log)
    if (i > 0) {
      const char *ts_curr = extract_timestamp_from_message(merged[i].message);
      const char *ts_prev = extract_timestamp_from_message(merged[output_idx - 1].message);
      if (ts_curr && ts_prev) {
        // Compare first 23 characters (YYYY-MM-DD HH:MM:SS.mmm)
        if (strncmp(ts_curr, ts_prev, 23) == 0) {
          continue; // Same timestamp, skip duplicate
        }
      }
    }

    if (output_idx != i) {
      merged[output_idx] = merged[i];
    }
    output_idx++;
  }

  *out_merged = merged;
  return output_idx;
}
