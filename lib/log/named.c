/**
 * @file lib/log/named.c
 * @brief Format named objects in log messages (replaces hex addresses with type/name descriptions)
 * @ingroup logging
 *
 * This module scans log message text for hex addresses (0x[0-9a-fA-F]+) and replaces
 * them with friendly descriptions from the named object registry when available.
 * For example, 0x7f1234567890 might become "mutex/recv_mutex.2 (0x7f1234567890)"
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "ascii-chat/debug/named.h"

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
 * @brief Check if an integer is a registered file descriptor
 * @param fd File descriptor value
 * @return true if the fd is registered as an FD type
 */
static bool is_registered_fd(int fd) {
  const char *type = named_get_type((uintptr_t)fd);
  return type && strcmp(type, "fd") == 0;
}

/**
 * @brief Check if current position matches FD prefix pattern
 * @param p Current position in string
 * @return true if preceded by fd/file/descriptor keywords
 */
static bool has_fd_prefix(const char *start, const char *p) {
  if (p == start) return false;  /* At beginning, no prefix */

  const char *word_start = p - 1;
  /* Skip back over whitespace, '=', ':' */
  while (word_start > start && (*word_start == ' ' || *word_start == '\t' ||
                                 *word_start == '=' || *word_start == ':')) {
    word_start--;
  }

  /* Check if we have a digit (we're in a decimal number) */
  if (!isdigit(*word_start)) {
    /* Not a digit, so look for fd/file/descriptor keywords before us */
    const char *check = word_start;
    while (check > start && (isalnum(*check) || *check == '_')) {
      check--;
    }
    check++;  /* Move back to start of word */

    size_t word_len = word_start - check + 1;
    if (word_len >= 2) {
      if ((word_len == 2 && strncasecmp(check, "fd", 2) == 0) ||
          (word_len == 4 && strncasecmp(check, "file", 4) == 0) ||
          (word_len == 10 && strncasecmp(check, "descriptor", 10) == 0)) {
        return true;
      }
    }
  }

  return false;
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
          /* This is a registered named object - format it */
          const char *fmt_spec = named_get_format_spec(address);
          if (!fmt_spec) {
              fmt_spec = "0x%tx";  /* Default format for addresses */
          }

          /* Format: "type/name (formatted_value)" */
          int written = snprintf(output + out_pos, output_size - out_pos, "%s/%s (", type, name);
          if (written > 0 && (size_t)written < output_size - out_pos) {
            out_pos += written;
            /* Format the value using the registered format spec */
            written = snprintf(output + out_pos, output_size - out_pos, fmt_spec, (ptrdiff_t)address);
            if (written > 0 && (size_t)written < output_size - out_pos) {
              out_pos += written;
              /* Add closing paren */
              if (out_pos + 1 < output_size) {
                output[out_pos++] = ')';
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
      /* Look for decimal integers that might be FDs */
      const char *int_start = p;
      int fd_value = 0;
      int digit_count = 0;

      /* Parse decimal digits */
      while (*p && isdigit(*p) && digit_count < 6) {  /* FDs rarely exceed 999999 */
        fd_value = fd_value * 10 + (*p - '0');
        p++;
        digit_count++;
      }

      if (digit_count > 0 && is_registered_fd(fd_value) &&
          has_fd_prefix(message, int_start)) {
        /* This integer is a registered FD with appropriate prefix - format it */
        const char *name = named_get(fd_value);
        const char *type = named_get_type(fd_value);
        const char *fmt_spec = named_get_format_spec(fd_value);

        if (!fmt_spec) {
          fmt_spec = "%d";  /* Default format for FDs */
        }

        if (name && type) {
          /* Format: "fd/name (value)" */
          int written = snprintf(output + out_pos, output_size - out_pos, "%s/%s (", type, name);
          if (written > 0 && (size_t)written < output_size - out_pos) {
            out_pos += written;
            written = snprintf(output + out_pos, output_size - out_pos, fmt_spec, fd_value);
            if (written > 0 && (size_t)written < output_size - out_pos) {
              out_pos += written;
              if (out_pos + 1 < output_size) {
                output[out_pos++] = ')';
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
 * Returns the original message if no formatting was applied.
 */
const char *log_named_format_or_original(const char *message) {
  if (!message) {
    return message;
  }

  static _Thread_local char format_buffer[NAMED_FORMAT_BUFFER_SIZE];
  int result = log_named_format_message(message, format_buffer, sizeof(format_buffer));

  if (result > 0) {
    return format_buffer;
  }

  return message;
}
