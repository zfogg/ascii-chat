/* Advertise distinct LAN endpoints without starting a server per picker row. */
#include <ascii-chat/common.h>
#include <ascii-chat/network/mdns/mdns.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc < 2 || asciichat_shared_init(NULL, false, true) != ASCIICHAT_OK)
    return 1;
  asciichat_mdns_t **contexts = calloc((size_t)argc - 1, sizeof(*contexts));
  if (!contexts)
    return 1;
  int result = 0;
  for (int i = 1; i < argc; ++i) {
    char name[64];
    snprintf(name, sizeof(name), "ascii-chat-picker-%d", i);
    unsigned long port = strtoul(argv[i], NULL, 10);
    asciichat_mdns_service_t service = {
        .name = name,
        .type = "_ascii-chat._tcp",
        .host = "localhost",
        .port = (uint16_t)port,
    };
    contexts[i - 1] = asciichat_mdns_init();
    if (!contexts[i - 1] || port == 0 || port > 65535 ||
        asciichat_mdns_advertise(contexts[i - 1], &service) != ASCIICHAT_OK) {
      result = 1;
      break;
    }
  }
  if (!result) {
    puts("READY");
    fflush(stdout);
    (void)getchar();
  }
  for (int i = 1; i < argc; ++i)
    if (contexts[i - 1])
      asciichat_mdns_destroy(contexts[i - 1]);
  free(contexts);
  asciichat_shared_destroy();
  return result;
}
