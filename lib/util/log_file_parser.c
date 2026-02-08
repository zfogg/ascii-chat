/**
 * @file util/log_file_parser.c
 * @brief Log file parsing and tailing implementation
 */

#include <ascii-chat/util/log_file_parser.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// Parse a single log line in format: [TIMESTAMP] [LEVEL] message...
// Example: [2026-02-08 12:34:56.789] [INFO] Server started
bool log_file_parser_parse_line(const char *line, session_log_entry_t *out_entry) {
  if (!line || !out_entry || !*line) {
    return false;
  }

  memset(out_entry, 0, sizeof(*out_entry));

  // Skip empty lines and whitespace-only lines
  if (strspn(line, " \t\r\n") == strlen(line)) {
    return false;
  }

  // Expected format: [TIMESTAMP] [LEVEL] message
  // We need at least: [X] [X] X (minimum 9 chars)
  if (strlen(line) < 9) {
    return false;
  }

  // Check for opening bracket
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

  // Message starts after "] " following the level bracket
  const char *message_start = bracket2 + 1;
  if (*message_start == ' ') {
    message_start++;
  }

  // Skip if message is empty
  if (!*message_start) {
    return false;
  }

  // Copy message to output, always bounded to prevent overflow
  strncpy(out_entry->message, message_start, SESSION_LOG_LINE_MAX - 1);
  out_entry->message[SESSION_LOG_LINE_MAX - 1] = '\0';

  // Remove trailing newline if present
  size_t len = strlen(out_entry->message);
  if (len > 0 && out_entry->message[len - 1] == '\n') {
    out_entry->message[len - 1] = '\0';
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
    log_debug("Cannot seek to tail position in log file: %s (errno: %s)", file_path, SAFE_STRERROR(errno));
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

  // Copy and recolor file entries (plain text -> colored)
  for (size_t i = 0; i < file_count; i++) {
    merged[buffer_count + i] = file_entries[i];

    // Recolor the plain text entry
    char colored_buf[SESSION_LOG_LINE_MAX];
    size_t colored_len = log_recolor_plain_entry(file_entries[i].message, colored_buf, sizeof(colored_buf));
    if (colored_len > 0 && colored_len < SESSION_LOG_LINE_MAX) {
      strcpy(merged[buffer_count + i].message, colored_buf);
    }
    // If recoloring fails, keep the original plain text
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
