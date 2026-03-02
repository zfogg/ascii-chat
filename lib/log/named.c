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
#include "ascii-chat/log/log.h"

/* Use a thread-local buffer to avoid allocations in the hot path */
#define NAMED_FORMAT_BUFFER_SIZE (4096)

/**
 * @brief Check if a type string is actually registered in the named registry
 * @param type_str The type string to validate (e.g., "thread", "mutex", "socket")
 * @param type_len Length of the type string
 * @return true if the type exists in any registered entry
 *
 * Queries the registry programmatically to ensure the type is actually registered,
 * preventing false positives on random "word/word" text patterns.
 */
struct type_check_context {
  const char *type_to_find;
  size_t type_len;
  bool found;
};

static void check_type_callback(uintptr_t key, const char *name, void *user_data) {
  (void)key;  /* unused */
  (void)name; /* unused */
  struct type_check_context *ctx = (struct type_check_context *)user_data;

  if (!ctx->found) {
    const char *type = named_get_type(key);
    if (type && strlen(type) == ctx->type_len && strncmp(type, ctx->type_to_find, ctx->type_len) == 0) {
      ctx->found = true;
    }
  }
}

static bool is_type_in_registry(const char *type_str, size_t type_len) {
  struct type_check_context ctx = {.type_to_find = type_str, .type_len = type_len, .found = false};

  named_registry_for_each(check_type_callback, &ctx);
  return ctx.found;
}

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
 * @brief Find any type prefix before an integer (socket, client, connection, fd, etc.)
 * @param start Start of string
 * @param p Current position in string (at the integer)
 * @param out_type Output buffer for the type name found
 * @param out_type_len Output for length of type name
 * @return Pointer to start of prefix, or p if no prefix found
 *
 * Generic function that detects common keywords: socket, client, connection, fd, file, etc.
 * Returns the type name in out_type for registry lookup.
 */
static const char *find_generic_type_prefix(const char *start, const char *p, const char **out_type,
                                            size_t *out_type_len) {
  if (p == start) {
    *out_type = NULL;
    *out_type_len = 0;
    return p;
  }

  const char *word_start = p - 1;
  /* Skip back over whitespace, '=', ':' */
  while (word_start > start &&
         (*word_start == ' ' || *word_start == '\t' || *word_start == '=' || *word_start == ':')) {
    word_start--;
  }

  /* Check if we have a digit (we're in a decimal number) */
  if (!isdigit(*word_start)) {
    /* Not a digit, so look for type keywords before us */
    const char *check = word_start;
    while (check > start && (isalnum(*check) || *check == '_')) {
      check--;
    }
    check++;

    const char *type_start = check;
    size_t type_len = word_start - type_start + 1;

    /* Check for known type keywords */
    static const struct {
      const char *type;
      size_t len;
    } known_types[] = {
        {"socket", 6}, {"client", 6},  {"connection", 10}, {"fd", 2},      {"file", 4},    {"descriptor", 11},
        {"thread", 6}, {"decoder", 7}, {"encoder", 7},     {"context", 7}, {"handler", 7},
    };

    for (size_t i = 0; i < sizeof(known_types) / sizeof(known_types[0]); i++) {
      if (type_len == known_types[i].len && strncmp(type_start, known_types[i].type, type_len) == 0) {
        *out_type = type_start;
        *out_type_len = type_len;
        return type_start;
      }
    }
  }

  *out_type = NULL;
  *out_type_len = 0;
  return p;
}

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
    /* Not a digit, so look for fd/file/descriptor/socket/sockfd keywords before us */
    const char *check = word_start;
    while (check > start && (isalnum(*check) || *check == '_')) {
      check--;
    }
    check++; /* Move back to start of word */

    size_t word_len = word_start - check + 1;
    if (word_len >= 2) {
      if ((word_len == 2 && strncasecmp(check, "fd", 2) == 0) ||
          (word_len == 4 && strncasecmp(check, "file", 4) == 0) ||
          (word_len == 6 && strncasecmp(check, "socket", 6) == 0) ||
          (word_len == 6 && strncasecmp(check, "sockfd", 6) == 0) ||
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

/* ============================================================================
 * Granular Helper Functions for Message Transformation
 * ============================================================================
 */


/* --- Decimal parsing --- */
static int parse_decimal_digits(const char *start, const char **end) {
  int value = 0;
  int count = 0;
  const char *p = start;
  while (*p && isdigit(*p) && count < 6) {
    value = value * 10 + (*p - '0');
    p++;
    count++;
  }
  *end = p;
  return value;
}


static bool write_formatted_packet_type(int pkt_value, const char *name,
                                         char *output, size_t output_size, size_t *out_pos) {
  const char *fmt_spec = named_get_packet_type_format_spec(pkt_value);
  if (!fmt_spec) {
    fmt_spec = "%d";
  }

  char id_buffer[64];
  int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, pkt_value);
  if (id_written <= 0 || id_written >= (int)sizeof(id_buffer)) {
    return false;
  }

  char temp_output[512];
  int temp_written = snprintf(temp_output, sizeof(temp_output), "packet_type/%s (pkt_type=%s)", name, id_buffer);
  if (temp_written <= 0 || (size_t)temp_written >= sizeof(temp_output)) {
    return false;
  }

  if (*out_pos + temp_written >= output_size - 1) {
    return false;
  }

  memcpy(output + *out_pos, temp_output, temp_written);
  *out_pos += temp_written;
  return true;
}

static bool write_formatted_fd(int fd_value, const char *name,
                                char *output, size_t output_size, size_t *out_pos) {
  const char *fmt_spec = named_get_fd_format_spec(fd_value);
  if (!fmt_spec) {
    fmt_spec = "%d";
  }

  char id_buffer[64];
  int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, fd_value);
  if (id_written <= 0 || id_written >= (int)sizeof(id_buffer)) {
    return false;
  }

  char temp_output[512];
  int temp_written = snprintf(temp_output, sizeof(temp_output), "fd/%s (fd=%s)", name, id_buffer);
  if (temp_written <= 0 || (size_t)temp_written >= sizeof(temp_output)) {
    return false;
  }

  if (*out_pos + temp_written >= output_size - 1) {
    return false;
  }

  memcpy(output + *out_pos, temp_output, temp_written);
  *out_pos += temp_written;
  return true;
}

static bool copy_unformatted_decimal(const char *int_start, const char *int_end,
                                      char *output, size_t output_size, size_t *out_pos) {
  size_t int_len = int_end - int_start;
  if (int_len >= output_size - *out_pos) {
    return false;
  }
  memcpy(output + *out_pos, int_start, int_len);
  *out_pos += int_len;
  return true;
}

/* --- Type= pattern detection and parsing --- */
static bool is_type_equals_pattern(const char *p) {
  return p[0] == 't' && p[1] == 'y' && p[2] == 'p' && p[3] == 'e' && p[4] == '=';
}

static bool type_equals_inside_parens(const char *p, const char *message) {
  if (p <= message) {
    return false;
  }

  const char *back = p - 1;
  while (back > message && isspace(*back)) {
    back--;
  }
  return back >= message && *back == '(';
}

static bool write_formatted_type_equals(int pkt_value, const char *name,
                                         char *output, size_t output_size, size_t *out_pos) {
  const char *fmt_spec = named_get_packet_type_format_spec(pkt_value);
  if (!fmt_spec) {
    fmt_spec = "%d";
  }

  char id_buffer[64];
  int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, pkt_value);
  if (id_written <= 0 || id_written >= (int)sizeof(id_buffer)) {
    return false;
  }

  char temp_output[512];
  int temp_written = snprintf(temp_output, sizeof(temp_output), "packet_type/%s (type=%s)", name, id_buffer);
  if (temp_written <= 0 || (size_t)temp_written >= sizeof(temp_output)) {
    return false;
  }

  if (*out_pos + temp_written >= output_size - 1) {
    return false;
  }

  memcpy(output + *out_pos, temp_output, temp_written);
  *out_pos += temp_written;
  return true;
}

/* --- Buffer operations --- */

static void copy_char_to_output(char *output, size_t *out_pos, char c) {
  output[(*out_pos)++] = c;
}

/* ============================================================================
 * Message Transformation Helpers
 * ============================================================================
 */

/**
 * @brief Try to format a hex address at the current position
 * @param message Start of message (for context lookback)
 * @param hex_start Start of hex sequence (pointing to '0' in "0x...")
 * @param output Output buffer
 * @param output_size Output buffer size
 * @param out_pos Current position in output (updated if transformation happens)
 * @param p Current position in input (updated to skip past hex address)
 * @return true if transformation was applied, false otherwise
 */
static bool try_format_hex_address(const char *message, const char *hex_start, char *output,
                                    size_t output_size, size_t *out_pos, const char **p) {
  const char *hex_ptr = hex_start + 2; /* Skip "0x" */

  /* Parse hex digits */
  uintptr_t address = 0;
  int hex_count = 0;
  while (*hex_ptr && isxdigit(*hex_ptr) && hex_count < 16) {
    address = (address << 4) | (unsigned int)(*hex_ptr > '9' ? (*hex_ptr | 0x20) - 'a' + 10 : *hex_ptr - '0');
    hex_ptr++;
    hex_count++;
  }

  if (hex_count == 0) {
    return false; /* No valid hex digits */
  }

  /* Check if already inside parentheses (part of previous formatting) */
  bool is_already_in_parens = (hex_start > message && *(hex_start - 1) == '(');
  if (is_already_in_parens) {
    return false;
  }

  /* Check if address is registered */
  const char *name = named_get(address);
  const char *type = named_get_type(address);
  if (!name || !type) {
    return false;
  }

  /* Format the output */
  const char *fmt_spec = named_get_format_spec(address);
  if (!fmt_spec) {
    fmt_spec = "0x%tx";
  }

  char id_buffer[128];
  int id_written = snprintf(id_buffer, sizeof(id_buffer), fmt_spec, (ptrdiff_t)address);
  if (id_written <= 0 || id_written >= (int)sizeof(id_buffer)) {
    return false;
  }

  char temp_output[512];
  int temp_written = snprintf(temp_output, sizeof(temp_output), "%s/%s (%s)", type, name, id_buffer);
  if (temp_written <= 0 || (size_t)temp_written >= sizeof(temp_output)) {
    return false;
  }

  if (*out_pos + temp_written >= output_size - 1) {
    return false; /* Buffer overflow */
  }

  memcpy(output + *out_pos, temp_output, temp_written);
  *out_pos += temp_written;
  *p = hex_ptr;
  return true;
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
      const char *hex_start = p;
      if (try_format_hex_address(message, hex_start, output, output_size, &out_pos, &p)) {
        any_transformed = true;
        continue;
      }

      /* Not registered or formatting failed - copy original hex address */
      const char *hex_end = hex_start + 2; /* Start after "0x" */
      while (*hex_end && isxdigit(*hex_end)) {
        hex_end++;
      }
      size_t hex_len = hex_end - hex_start;
      if (hex_len < output_size - out_pos) {
        memcpy(output + out_pos, hex_start, hex_len);
        out_pos += hex_len;
        p = hex_end;
      } else {
        /* Buffer overflow - truncate and stop */
        break;
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

      /* Skip if this integer is part of already-formatted output like "(fd=20)" or "(type/name (...))" */
      bool is_already_formatted = false;
      if (int_start - message >= 5) {
        /* Check for "(fd=", "(pkt_type=", "(socket=", "(sockfd=", or "(type/" patterns to prevent re-formatting.
         * Named objects are formatted as: type/name (0xaddress) or type/name (key=value)
         * We need to detect if we're already inside a formatted output.
         */
        const char *check = int_start - 1;
        while (check > message && isspace(*check))
          check--;

        // Only access *check if pointer is within bounds
        if (check >= message && *check == '=') {
          /* Found "=", now check what prefix it has for patterns like "(fd=20)" or "(socket=20)" or "(pkt_type=123)" */
          const char *eq_pos = check;
          check--;
          while (check > message && (isalnum(*check) || *check == '_')) {
            check--;
          }
          check++;

          size_t prefix_len = eq_pos - check;
          if ((prefix_len == 2 && strncmp(check, "fd", 2) == 0) ||
              (prefix_len == 6 && strncmp(check, "socket", 6) == 0) ||
              (prefix_len == 6 && strncmp(check, "sockfd", 6) == 0) ||
              (prefix_len == 8 && strncmp(check, "pkt_type", 8) == 0)) {
            is_already_formatted = true;
          }
        } else if (check >= message && *check == '/') {
          /* Found "/", now check if this is a type/name pattern.
           * Format is "(type/name (...)" where type is word characters (thread, mutex, socket, etc.)
           * Must verify "/" is preceded by word characters AND "(" to ensure it's a formatted type.
           */
          const char *slash_pos = check;
          check--;
          while (check > message && (isalnum(*check) || *check == '_')) {
            check--;
          }
          check++;

          /* Check that the "/" was preceded by word characters (indicating a type name) */
          size_t type_len = slash_pos - check;
          if (type_len > 0 && (isalpha(*check) || *check == '_')) {
            /* Verify this is a registered type/name pattern by checking for "(" before the type
             * AND checking if the type actually exists in the registry */
            const char *type_start = check;
            const char *before_type = type_start - 1;
            while (before_type > message && isspace(*before_type)) {
              before_type--;
            }

            /* Only mark as formatted if:
             * 1. Preceded by "(" - ensures pattern looks like "(type/name ...)"
             * 2. Type exists in registry - prevents false positives on random text */
            if (before_type >= message && *before_type == '(' && is_type_in_registry(type_start, type_len)) {
              is_already_formatted = true;
            }
          }
        }
      }

      /* Check if this is a registered packet type */
      if (!is_already_formatted && digit_count > 0 && has_packet_type_prefix(message, int_start)) {
        const char *name = named_get_packet_type(fd_value);
        if (name) {
          const char *prefix_start = find_packet_type_prefix_start(message, int_start);
          size_t prefix_len = int_start - prefix_start;
          size_t saved_pos = out_pos;

          if (out_pos >= prefix_len) {
            out_pos -= prefix_len;
          }

          if (write_formatted_packet_type(fd_value, name, output, output_size, &out_pos)) {
            any_transformed = true;
            continue;
          }

          out_pos = saved_pos; /* Restore on failure */
        }
      }


      /* Check if this is a registered file descriptor */
      if (!is_already_formatted && digit_count > 0 && has_fd_prefix(message, int_start)) {
        const char *name = named_get_fd(fd_value);
        if (name) {
          const char *prefix_start = find_fd_prefix_start(message, int_start);
          size_t prefix_len = int_start - prefix_start;
          size_t saved_pos = out_pos;

          if (out_pos >= prefix_len) {
            out_pos -= prefix_len;
          }

          if (write_formatted_fd(fd_value, name, output, output_size, &out_pos)) {
            any_transformed = true;
            continue;
          }

          out_pos = saved_pos; /* Restore on failure */
        }
      }

      /* Check if this integer has a generic type prefix (socket, client, connection, etc.)
       * DISABLED: This feature has a backtracking bug that corrupts output when applied after
       * other formatting operations. Re-enable only after fixing the backtracking logic to
       * properly track correspondence between input and output positions.
       * See commit b86fed2a8 which introduced the bug.
       */
      if (false && !is_already_formatted && digit_count > 0) {
        const char *type_name = NULL;
        size_t type_len = 0;
        const char *prefix_start = find_generic_type_prefix(message, int_start, &type_name, &type_len);

        if (type_name && prefix_start != int_start) {
          /* Skip if already formatted with a name (e.g., "socket/listener.0" already has a /) */
          bool skip_format = false;
          /* Check if this looks like it's already been formatted by looking for / in the pattern */
          const char *check_ptr = prefix_start;
          while (check_ptr < int_start && *check_ptr && *check_ptr != '/') {
            check_ptr++;
          }
          if (check_ptr < int_start && *check_ptr == '/') {
            /* Already formatted (contains /), skip */
            skip_format = true;
          }

          if (!skip_format) {
            /* We found a type prefix like "socket", "client", etc.
             * Try to look up a registered name for this type+id */

            /* Normalize type name aliases: "sockfd" → "socket", "descriptor" → "fd" */
            const char *lookup_type = type_name;
            size_t lookup_len = type_len;
            if (type_len == 6 && strncmp(type_name, "sockfd", 6) == 0) {
              lookup_type = "socket";
              lookup_len = 6;
            } else if (type_len == 10 && strncmp(type_name, "descriptor", 10) == 0) {
              lookup_type = "fd";
              lookup_len = 2;
            }

            const char *name = named_get_by_type_and_id(lookup_type, lookup_len, fd_value);

            char temp_output[512];
            char type_str[32];
            char id_buffer[64];

            /* Safely copy type name */
            if (type_len < sizeof(type_str)) {
              memcpy(type_str, type_name, type_len);
              type_str[type_len] = '\0';

              int id_written = snprintf(id_buffer, sizeof(id_buffer), "%d", fd_value);
              if (id_written > 0 && id_written < (int)sizeof(id_buffer)) {
                int temp_written = 0;
                if (name) {
                  /* Format with name: type/name (type=value) */
                  temp_written =
                      snprintf(temp_output, sizeof(temp_output), "%s/%s (%s=%s)", type_str, name, type_str, id_buffer);

                  if (temp_written > 0 && (size_t)temp_written < sizeof(temp_output)) {
                    /* Backtrack to remove the prefix we already copied */
                    size_t prefix_len = int_start - prefix_start;
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
          }
        }
      }

      /* Not a registered FD or formatting failed - copy original number */
      if (!copy_unformatted_decimal(int_start, p, output, output_size, &out_pos)) {
        break;
      }
    } else {
      /* Check if this is the start of "type=DIGIT" pattern */
      if (*p == 't' && p + 4 < message + strlen(message) && is_type_equals_pattern(p)) {
        if (!type_equals_inside_parens(p, message)) {
          const char *digit_end = p + 5;
          int pkt_value = parse_decimal_digits(digit_end, &digit_end);

          if (pkt_value > 0) {
            const char *name = named_get_packet_type(pkt_value);
            if (name && write_formatted_type_equals(pkt_value, name, output, output_size, &out_pos)) {
              p = digit_end;
              any_transformed = true;
              continue;
            }
          }
        }
      }

      /* Regular character - copy it */
      copy_char_to_output(output, &out_pos, *p++);
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
