#include <ascii-chat/version.h>
#include <stdio.h>

int main(void) {
  const char *version = ascii_chat_get_version();
  if (!version || !version[0]) {
    return 1;
  }
  puts(version);
  return 0;
}
