/** @file platform/terminal_format.c
 * @brief Portable terminal hyperlink and text attribute formatting.
 */
#include <ascii-chat/platform/terminal.h>
#include <stdio.h>

int terminal_format_hyperlink(char *buffer, size_t capacity, const char *url, const char *label, bool enabled) {
  if (!buffer || !capacity)
    return -1;
  buffer[0] = '\0';
  if (!url || !*url || !label)
    return -1;
  const char *parts[] = {url, label};
  for (size_t i = 0; i < 2; ++i) {
    for (const unsigned char *p = (const unsigned char *)parts[i]; *p; ++p) {
      if (*p < 32 || *p == 127)
        return -1;
    }
  }
  int length = enabled ? snprintf(buffer, capacity, "\033]8;;%s\033\\\033[4m%s\033[24m\033]8;;\033\\", url, label)
                       : snprintf(buffer, capacity, "%s", label);
  if (length < 0 || (size_t)length >= capacity) {
    buffer[0] = '\0';
    return -1;
  }
  return length;
}
