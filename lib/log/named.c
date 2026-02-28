/**
 * @file lib/log/named.c
 * @brief Format named objects in log messages (replaces hex addresses with type/name descriptions)
 * @ingroup logging
 *
 * This module scans log message text for hex addresses (0x[0-9a-fA-F]+) and replaces
 * them with friendly descriptions from the named object registry when available.
 * For example, 0x7f1234567890 might become "mutex/recv_mutex.2 (0x7f1234567890)"
 *
 * Output is colorized for better readability:
 * - Type name: yellow (LOG_COLOR_WARN)
 * - Name: orange (LOG_COLOR_DEV)
 * - ID value: grey (LOG_COLOR_GREY)
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "ascii-chat/debug/named.h"
#include "ascii-chat/util/string.h"
#include "ascii-chat/log/logging.h"

/* Use a thread-local buffer to avoid allocations in the hot path */
#define NAMED_FORMAT_BUFFER_SIZE (4096)

/**
 * @brief Format hex addresses in a message as named object descriptions
 * @param message Input message (may contain hex addresses)
 * @param output Output buffer for formatted message
 * @param output_size Size of output buffer
 * @return Length of output string (excluding null terminator), -1 on error or no formatting applied
 *
 * Scans for patterns like 0x[0-9a-fA-F]+ and checks if they're registered named objects.
 * If found, replaces with "type/name (0xaddress)" format.
 * Otherwise, leaves the address as-is.
 */

/**
 * @brief Find the start of the fd/file/descriptor prefix before a position
 * @param start Start of string
 * @param p Current position in string
 * @return Pointer to start of prefix (fd/file/descriptor), or p if no prefix found
 *
 * Walks backwards from position p to find fd/file/descriptor keyword.
 * Returns pointer to the first character of the keyword.
 */
static const char *find_fd_prefix_start(const char *start, const char *p) {
  if (p == start)
    return p;

  const char *word_start = p - 1;
  /* Skip back over whitespace, '=', ':' */
  while (word_start > start &&
         (*word_start == ' ' || *word_start == '\t' || *word_start == '=' || *word_start == ':')) {
    word_start--;
  }

  /* Check if we have a digit (we're in a decimal number) */
  if (!isdigit(*word_start)) {
    /* Not a digit, so look for fd/file/descriptor keywords before us */
    const char *check = word_start;
    while (check > start && (isalnum(*check) || *check == '_')) {
      check--;
    }
    check++; /* Move back to start of word */

    size_t word_len = word_start - check + 1;
    if (word_len >= 2) {
      if ((word_len == 2 && strncasecmp(check, "fd", 2) == 0) ||
          (word_len == 4 && strncasecmp(check, "file", 4) == 0) ||
          (word_len == 10 && strncasecmp(check, "descriptor", 10) == 0)) {
        return check; /* Return start of keyword */
      }
    }
  }

  return p; /* No prefix found, return original position */
}

/**
 * @brief Find the start of the packet type prefix before a position
 * @param start Start of string
 * @param p Current position in string
 * @return Pointer to start of prefix (packet/packet type), or p if no prefix found
 *
 * Walks backwards from position p to find "packet type", "packet (%d)", "packet %d", etc.
 * Returns pointer to the first character of the keyword.
 */
static const char *find_packet_type_prefix_start(const char *start, const char *p) {
  if (p == start)
    return p;

  const char *word_start = p - 1;
  /* Skip back over whitespace, '=', ':', '(' */
  while (word_start > start && (*word_start == ' ' || *word_start == '\t' || *word_start == '=' || *word_start == ':' ||
                                *word_start == '(')) {
    word_start--;
  }

  /* Check if we have a digit (we're in a decimal number) */
  if (!isdigit(*word_start)) {
    /* Not a digit, so look for packet type keywords before us */
    const char *check = word_start;
    while (check > start && (isalnum(*check) || *check == '_')) {
      check--;
    }
    check++; /* Move back to start of word */

    size_t word_len = word_start - check + 1;
    /* Check for "type" keyword (part of "packet type") */
    if (word_len >= 4 && strncasecmp(check, "type", 4) == 0) {
      /* Look for "packet" before "type" */
      const char *packet_check = check - 1;
      while (packet_check > start && (*packet_check == ' ' || *packet_check == '\t')) {
        packet_check--;
      }
      if (packet_check > start) {
        const char *packet_start = packet_check;
        while (packet_start > start && (isalnum(*packet_start) || *packet_start == '_')) {
          packet_start--;
        }
        packet_start++;
        size_t packet_len = packet_check - packet_start + 1;
        if (packet_len == 6 && strncasecmp(packet_start, "packet", 6) == 0) {
          return packet_start; /* Return start of "packet" in "packet type" */
        }
      }
    }
    /* Check for "packet" keyword alone */
    if (word_len >= 6 && strncasecmp(check, "packet", 6) == 0) {
      return check; /* Return start of "packet" */
    }
  }

  return p; /* No prefix found, return original position */
}

/**
 * @brief Check if current position matches packet type prefix pattern
 * @param p Current position in string
 * @return true if preceded by packet type keywords
 */
static bool has_packet_type_prefix(const char *start, const char *p) {
  return find_packet_type_prefix_start(start, p) != p;
}

/**
 * @brief Check if current position matches FD prefix pattern
 * @param p Current position in string
 * @return true if preceded by fd/file/descriptor keywords
 */
static bool has_fd_prefix(const char *start, const char *p) {
  return find_fd_prefix_start(start, p) != p;
}

int log_named_format_message(const char *message, char *output, size_t output_size) {
  if (!message || !output || output_size == 0) {
    return -1;
  }

  size_t out_pos = 0;
  const char *p = message;
  bool any_transformed = false;

  while (*p && out_pos < output_size - 1) {
    /* Look for "0x" prefix indicating a hex address */
    if (*p == '0' && *(p + 1) == 'x' && isxdigit(*(p + 2))) {
      /* Found potential hex address - parse it */
      const char *hex_start = p;
      p += 2; /* Skip "0x" */

      /* Parse hex digits */
      uintptr_t address = 0;
      int hex_count = 0;
      while (*p && isxdigit(*p) && hex_count < 16) {
        address = (address << 4) | (unsigned int)(*p > '9' ? (*p | 0x20) - 'a' + 10 : *p - '0');
        p++;
        hex_count++;
      }

      if (hex_count > 0) {
        /* We parsed a valid hex number - check if it's in the registry */
        const char *name = named_get(address);
        const char *type = named_get_type(address);

        if (name && type) {
          /* Format: type/name (address) - plain text, colors applied at output stage */
          const char *fmt_spec = named_get_format_spec(address);
          if (!fmt_spec) {
            fmt_spec = "0x%tx"; /* Default format for addresses */
          }

          char id_buffer[128];
          int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, (ptrdiff_t)address);
          if (id_written > 0 && id_written < (int)sizeof(id_buffer)) {
            // Format without colors - let the logging system colorize the output
            char temp_output[512];
            int temp_written = snprintf(temp_output, sizeof(temp_output), "%s/%s (%s)", type, name, id_buffer);
            if (temp_written > 0 && (size_t)temp_written < sizeof(temp_output)) {
              if (out_pos + temp_written < output_size - 1) {
                memcpy(output + out_pos, temp_output, temp_written);
                out_pos += temp_written;
                any_transformed = true;
                continue;
              }
            }
          }
        }

        /* Not registered or snprintf failed - copy original hex address */
        size_t hex_len = p - hex_start;
        if (hex_len < output_size - out_pos) {
          memcpy(output + out_pos, hex_start, hex_len);
          out_pos += hex_len;
        } else {
          /* Buffer overflow - truncate and stop */
          break;
        }
      }
    } else if (isdigit(*p)) {
      /* Look for decimal integers that might be FDs or packet types */
      const char *int_start = p;
      int fd_value = 0;
      int digit_count = 0;

      /* Parse decimal digits */
      while (*p && isdigit(*p) && digit_count < 6) { /* FDs rarely exceed 999999 */
        fd_value = fd_value * 10 + (*p - '0');
        p++;
        digit_count++;
      }

      /* Skip if this integer is part of already-formatted output like "(fd=20)" or "(pkt_type=123)" */
      bool is_already_formatted = false;
      if (int_start >= 5) {
        /* Check for "(fd=" or "(pkt_type=" patterns immediately before this integer */
        const char *check = int_start - 1;
        while (check > message && isspace(*check))
          check--;

        if (*check == '=') {
          /* Found "=", now check what prefix it has */
          const char *eq_pos = check;
          check--;
          while (check > message && (isalnum(*check) || *check == '_')) {
            check--;
          }
          check++;

          size_t prefix_len = eq_pos - check;
          if ((prefix_len == 2 && strncmp(check, "fd", 2) == 0) ||
              (prefix_len == 8 && strncmp(check, "pkt_type", 8) == 0)) {
            is_already_formatted = true;
          }
        }
      }

      /* Check if this is a registered packet type */
      if (!is_already_formatted && digit_count > 0 && has_packet_type_prefix(message, int_start)) {
        const char *name = named_get_packet_type(fd_value);
        if (name) {
          /* This integer is a registered packet type with appropriate prefix - format it */
          const char *fmt_spec = named_get_packet_type_format_spec(fd_value);
          if (!fmt_spec) {
            fmt_spec = "%d"; /* Default format for packet types */
          }

          /* Format: packet_type/name (pkt_type=value) - plain text, colors applied at output stage */
          char temp_output[512];
          char id_buffer[64];
          int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, fd_value);
          if (id_written > 0 && id_written < (int)sizeof(id_buffer)) {
            int temp_written =
                snprintf(temp_output, sizeof(temp_output), "packet_type/%s (pkt_type=%s)", name, id_buffer);
            if (temp_written > 0 && (size_t)temp_written < sizeof(temp_output)) {
              /* Find where prefix starts in original message, calculate how much to skip */
              const char *prefix_start = find_packet_type_prefix_start(message, int_start);
              size_t prefix_len = int_start - prefix_start; /* Length of prefix in original message */

              /* Backtrack in output buffer to remove the prefix we already copied */
              if (out_pos >= prefix_len) {
                out_pos -= prefix_len;
              }

              int copy_len = temp_written;
              if (out_pos + copy_len < output_size - 1) {
                memcpy(output + out_pos, temp_output, copy_len);
                out_pos += copy_len;
                any_transformed = true;
                continue;
              }
            }
          }
        }
      }

      /* Check if this is a registered file descriptor */
      if (!is_already_formatted && digit_count > 0 && has_fd_prefix(message, int_start)) {
        const char *name = named_get_fd(fd_value);
        if (name) {
          /* This integer is a registered FD with appropriate prefix - format it */
          const char *fmt_spec = named_get_fd_format_spec(fd_value);
          if (!fmt_spec) {
            fmt_spec = "%d"; /* Default format for FDs */
          }

          /* Format: fd/name (fd=value) - plain text, colors applied at output stage */
          char temp_output[512];
          char id_buffer[64];
          int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, fd_value);
          if (id_written > 0 && id_written < (int)sizeof(id_buffer)) {
            int temp_written = snprintf(temp_output, sizeof(temp_output), "fd/%s (fd=%s)", name, id_buffer);
            if (temp_written > 0 && (size_t)temp_written < sizeof(temp_output)) {
              /* Find where prefix starts in original message, calculate how much to skip */
              const char *prefix_start = find_fd_prefix_start(message, int_start);
              size_t prefix_len = int_start - prefix_start; /* Length of prefix in original message */

              /* Backtrack in output buffer to remove the prefix we already copied */
              if (out_pos >= prefix_len) {
                out_pos -= prefix_len;
              }

              int copy_len = temp_written;
              if (out_pos + copy_len < output_size - 1) {
                memcpy(output + out_pos, temp_output, copy_len);
                out_pos += copy_len;
                any_transformed = true;
                continue;
              }
            }
          }
        }
      }

      /* Not a registered FD or formatting failed - copy original number */
      size_t int_len = p - int_start;
      if (int_len < output_size - out_pos) {
        memcpy(output + out_pos, int_start, int_len);
        out_pos += int_len;
      } else {
        break;
      }
    } else {
      /* Regular character - copy it */
      output[out_pos++] = *p++;
    }
  }

  /* Null terminate */
  if (out_pos < output_size) {
    output[out_pos] = '\0';
  } else {
    output[output_size - 1] = '\0';
  }

  return any_transformed ? (int)out_pos : -1;
}

/**
 * @brief Format named objects in a message (thread-local buffer)
 * @param message Input message
 * @return Formatted message string (thread-local buffer, valid only until next call)
 *
 * Convenience wrapper that uses a thread-local buffer internally.
 * Applies formatting iteratively until the message stops changing (fixpoint).
 * Returns the original message if no formatting was applied.
 */
const char *log_named_format_or_original(const char *message) {
  if (!message) {
    return message;
  }

  static _Thread_local char format_buffer[NAMED_FORMAT_BUFFER_SIZE];
  static _Thread_local char work_buffer[NAMED_FORMAT_BUFFER_SIZE];

  /* Apply formatting iteratively until the message stabilizes (no more changes) */
  const char *current = message;
  size_t max_iterations = 10; /* Prevent infinite loops */

  for (size_t iter = 0; iter < max_iterations; iter++) {
    int result = log_named_format_message(current, format_buffer, sizeof(format_buffer));

    if (result <= 0) {
      /* No transformation was made, we've reached a fixpoint */
      return current == message ? message : format_buffer;
    }

    /* Check if the message changed */
    if (strcmp(current, format_buffer) == 0) {
      /* No actual change, return the current version */
      return format_buffer;
    }

    /* Message changed, prepare for next iteration */
    if (current == message) {
      /* First iteration, use format_buffer as input for next iteration */
      current = format_buffer;
    } else {
      /* Subsequent iterations, swap buffers */
      strcpy(work_buffer, format_buffer);
      current = work_buffer;
    }
  }

  /* Max iterations reached, return last formatted version */
  return format_buffer;
}

/**
 * @brief Colorize a "type/name (0xaddress)" string for display in memory reports
 * @param name_str String in "type/name (0xaddress)" format (e.g., "thread/splash_anim.0 (0x7f123456)")
 * @return Colorized string with ANSI codes: type(yellow)/name(blue) (id(grey))
 *
 * Used by memory report to colorize named object display.
 * Returns a pointer to a static rotating buffer (4 buffers, valid until next call).
 *
 * Example: "thread/splash_anim.0 (0x7f123456)" displays with:
 *   - "thread" in yellow
 *   - "splash_anim.0" in blue
 *   - "0x7f123456" in grey
 */
const char *colorize_named_string(const char *name_str) {
#define COLORIZE_BUFFERS 4
#define COLORIZE_BUFFER_SIZE 512
  static char buffers[COLORIZE_BUFFERS][COLORIZE_BUFFER_SIZE];
  static int buffer_idx = 0;

  if (!name_str || name_str[0] == '\0') {
    return name_str;
  }

  char *current_buf = buffers[buffer_idx];
  buffer_idx = (buffer_idx + 1) % COLORIZE_BUFFERS;

  /* Find the slash separator (type/name) */
  const char *slash = strchr(name_str, '/');
  if (!slash) {
    /* No slash found - just return the string as-is */
    return name_str;
  }

  /* Find opening paren for address */
  const char *paren = strchr(name_str, '(');

  /* Extract type (before slash) */
  size_t type_len = slash - name_str;
  char type_buf[256];
  strncpy(type_buf, name_str, type_len < sizeof(type_buf) - 1 ? type_len : sizeof(type_buf) - 1);
  type_buf[type_len] = '\0';

  /* Extract name (between slash and paren, or end of string) */
  const char *name_start = slash + 1;
  size_t name_len;
  if (paren && paren > slash) {
    /* Skip spaces before paren */
    const char *name_end = paren - 1;
    while (name_end > name_start && *name_end == ' ') {
      name_end--;
    }
    name_len = name_end - name_start + 1;
  } else {
    name_len = strlen(name_start);
  }

  char name_buf[256];
  strncpy(name_buf, name_start, name_len < sizeof(name_buf) - 1 ? name_len : sizeof(name_buf) - 1);
  name_buf[name_len] = '\0';

  /* Apply colors */
  const char *type_colored = colored_string(LOG_COLOR_WARN, type_buf);
  const char *name_colored = colored_string(LOG_COLOR_DEV, name_buf);

  /* Format: type(yellow)/name(blue) and add address part if present */
  if (paren) {
    /* Extract address (0x...) from parentheses */
    const char *addr_start = paren + 1;
    const char *addr_end = strchr(addr_start, ')');
    if (addr_end) {
      size_t addr_len = addr_end - addr_start;
      char addr_buf[128];
      strncpy(addr_buf, addr_start, addr_len < sizeof(addr_buf) - 1 ? addr_len : sizeof(addr_buf) - 1);
      addr_buf[addr_len] = '\0';

      const char *addr_colored = colored_string(LOG_COLOR_GREY, addr_buf);
      safe_snprintf(current_buf, COLORIZE_BUFFER_SIZE, "%s/%s (%s)", type_colored, name_colored, addr_colored);
    } else {
      safe_snprintf(current_buf, COLORIZE_BUFFER_SIZE, "%s/%s %s", type_colored, name_colored, paren);
    }
  } else {
    safe_snprintf(current_buf, COLORIZE_BUFFER_SIZE, "%s/%s", type_colored, name_colored);
  }

  return current_buf;
#undef COLORIZE_BUFFERS
#undef COLORIZE_BUFFER_SIZE
}
