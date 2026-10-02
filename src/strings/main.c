/**
 * @file strings/main.c
 * @ingroup tools
 * @brief TODO: Add Doxygen documentation for session string generation utility
 *
 * Command-line tool for generating memorable session identifiers (adjective-noun-noun).
 * Also exports wordlists as JavaScript for web client compatibility.
 *
 * Needs documentation for:
 * - CLI interface and options
 * - Session string format and constraints
 * - Wordlist export functionality
 * - Integration with ACDS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sodium.h>
#include <ascii-chat/common/error_codes.h>
#include <ascii-chat/discovery/adjectives.h>
#include <ascii-chat/discovery/nouns.h>

#define SESSION_STRING_BUFFER_SIZE 40
#define ACDS_MAX_UNIQUE_SESSIONS (2500LL * 5000LL * 5000LL)


int main(int argc, char *argv[]) {
  long long count = 1;
  int dump_adjectives = 0;
  int dump_nouns = 0;

  static struct option long_options[] = {
    {"count", required_argument, 0, 'n'},
    {"dump-adjectives", no_argument, 0, 'a'},
    {"dump-nouns", no_argument, 0, 'o'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  while ((opt = getopt_long(argc, argv, "n:aoh", long_options, NULL)) != -1) {
    switch (opt) {
      case 'n':
        count = strtoll(optarg, NULL, 10);
        break;
      case 'a':
        dump_adjectives = 1;
        break;
      case 'o':
        dump_nouns = 1;
        break;
      case 'h':
        printf("Usage: ascii-chat-strings [OPTIONS]\n");
        printf("Generate memorable session strings (adjective-noun-noun format)\n");
        printf("\nOptions:\n");
        printf("  -n, --count COUNT        Generate COUNT session strings (default: 1)\n");
        printf("  -a, --dump-adjectives    Dump adjectives list as JavaScript\n");
        printf("  -o, --dump-nouns         Dump nouns list as JavaScript\n");
        printf("  -h, --help               Show this help message\n");
        return 0;
      default:
        return ERROR_USAGE;
    }
  }

  // Handle dump modes
  if (dump_adjectives) {
    printf("export const adjectives = [\n");
    for (size_t i = 0; i < adjectives_count; i++) {
      printf("  \"%s\"%s\n", adjectives[i], (i < adjectives_count - 1) ? "," : "");
    }
    printf("];\n");
    printf("export const adjectives_count = %zu;\n", adjectives_count);
    return 0;
  }

  if (dump_nouns) {
    printf("export const nouns = [\n");
    for (size_t i = 0; i < nouns_count; i++) {
      printf("  \"%s\"%s\n", nouns[i], (i < nouns_count - 1) ? "," : "");
    }
    printf("];\n");
    printf("export const nouns_count = %zu;\n", nouns_count);
    return 0;
  }

  // Normal session string generation
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
