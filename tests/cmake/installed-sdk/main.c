#include <ascii-chat/version.h>
#include <stdio.h>

int main(void) {
  semantic_version_t version = version_parse(ASCII_CHAT_VERSION_STRING);
  if (!version.valid || version.major != ASCII_CHAT_VERSION_MAJOR) {
    return 1;
  }
  puts(ASCII_CHAT_VERSION_STRING);
  return 0;
}
