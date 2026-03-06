#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <ascii-chat/discovery/strings.h>

#define MAX_SESSIONS 1000

void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s [-n|--count COUNT] [-h|--help]\n", prog);
  fprintf(stderr, "  -n, --count COUNT   Generate COUNT session strings (default: 1)\n");
  fprintf(stderr, "  -h, --help          Show this help message\n");
}

int main(int argc, char *argv[]) {
  int count = 1;
  static struct option long_options[] = {
    {"count", required_argument, 0, 'n'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "n:h", long_options, NULL)) != -1) {
    switch (opt) {
      case 'n':
        count = atoi(optarg);
        break;
      case 'h':
        print_usage(argv[0]);
        return 0;
      default:
        print_usage(argv[0]);
        return 1;
    }
  }

  if (count <= 0) {
    fprintf(stderr, "Error: count must be greater than 0\n");
    return 1;
  }

  if (count > MAX_SESSIONS) {
    fprintf(stderr, "Error: count cannot exceed %d\n", MAX_SESSIONS);
    return 1;
  }

  // Initialize session string system
  asciichat_error_t err = acds_string_init();
  if (err != ASCIICHAT_OK) {
    fprintf(stderr, "Error: failed to initialize session string system\n");
    return 1;
  }

  // Generate session strings
  for (int i = 0; i < count; i++) {
    char session_string[SESSION_STRING_BUFFER_SIZE];
    err = acds_string_generate(session_string, sizeof(session_string));
    if (err != ASCIICHAT_OK) {
      fprintf(stderr, "Error: failed to generate session string\n");
      return 1;
    }
    printf("%s\n", session_string);
  }

  acds_strings_destroy();
  return 0;
}
