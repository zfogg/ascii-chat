#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/random.h>
#include <time.h>
#include <ascii-chat/common/error_codes.h>
#include <ascii-chat/discovery/adjectives.h>
#include <ascii-chat/discovery/nouns.h>

#define SESSION_STRING_BUFFER_SIZE 40
#define ACDS_MAX_UNIQUE_SESSIONS (2500LL * 5000LL * 5000LL)

// Simple RNG using getrandom
static uint32_t randombytes_uniform(uint32_t upper_bound) {
  if (upper_bound == 0) return 0;
  uint32_t random_val = 0;
  if (getrandom(&random_val, sizeof(random_val), 0) != sizeof(random_val)) {
    random_val = (uint32_t)time(NULL) ^ getpid();
  }
  return random_val % upper_bound;
}

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

  // Generate session strings
  for (long long i = 0; i < count; i++) {
    uint32_t adj_idx = randombytes_uniform((uint32_t)adjectives_count);
    uint32_t noun1_idx = randombytes_uniform((uint32_t)nouns_count);
    uint32_t noun2_idx = randombytes_uniform((uint32_t)nouns_count);

    printf("%s-%s-%s\n",
           adjectives[adj_idx],
           nouns[noun1_idx],
           nouns[noun2_idx]);
  }

  return 0;
}
