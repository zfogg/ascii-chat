#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ascii-chat/common/error_codes.h>
#include <ascii-chat/discovery/strings.h>

int main(int argc, char *argv[]) {
  long long count = 1;
  static struct option long_options[] = {
    {"count", required_argument, 0, 'n'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "n:h", long_options, NULL)) != -1) {
    switch (opt) {
      case 'n':
        count = strtoll(optarg, NULL, 10);
        break;
      case 'h':
        printf("Usage: ascii-chat-strings [-n|--count COUNT]\n");
        printf("Generate memorable session strings (adjective-noun-noun format)\n");
        printf("\nOptions:\n");
        printf("  -n, --count COUNT   Generate COUNT session strings (default: 1)\n");
        printf("  -h, --help          Show this help message\n");
        return 0;
      default:
        return ERROR_USAGE;
    }
  }

  if (count <= 0 || (long long)count > ACDS_MAX_UNIQUE_SESSIONS) {
    return ERROR_USAGE;
  }

  // Initialize session string system
  asciichat_error_t err = acds_string_init();
  if (err != ASCIICHAT_OK) {
    return err;
  }

  // Generate session strings
  for (long long i = 0; i < count; i++) {
    char session_string[SESSION_STRING_BUFFER_SIZE];
    err = acds_string_generate(session_string, sizeof(session_string));
    if (err != ASCIICHAT_OK) {
      acds_strings_destroy();
      return err;
    }
    printf("%s\n", session_string);
  }

  acds_strings_destroy();
  return 0;
}
