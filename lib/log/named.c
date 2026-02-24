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
