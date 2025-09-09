#ifdef _WIN32

#include "getopt_windows.h"
#include <stdio.h>
#include <string.h>

// Global variables for getopt
char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

// Internal state
static char *nextchar = NULL;

// Reset getopt state
static void reset_getopt(void) {
  nextchar = NULL;
}

// Basic getopt implementation
int getopt(int argc, char *const argv[], const char *optstring) {
  static int sp = 1;
  char *oli;

  if (sp == 1) {
    if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0') {
      return -1;
    } else if (strcmp(argv[optind], "--") == 0) {
      optind++;
      return -1;
    }
  }

  optopt = argv[optind][sp];
  oli = strchr(optstring, optopt);

  if (optopt == ':' || oli == NULL) {
    if (opterr && *optstring != ':') {
      fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], optopt);
    }
    if (argv[optind][++sp] == '\0') {
      optind++;
      sp = 1;
    }
    return '?';
  }

  if (*(++oli) != ':') {
    optarg = NULL;
    if (argv[optind][++sp] == '\0') {
      sp = 1;
      optind++;
    }
  } else {
    if (argv[optind][sp + 1] != '\0') {
      optarg = &argv[optind++][sp + 1];
    } else if (++optind >= argc) {
      if (*optstring == ':') {
        return ':';
      }
      if (opterr) {
        fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optopt);
      }
      sp = 1;
      return '?';
    } else {
      optarg = argv[optind++];
    }
    sp = 1;
  }

  return optopt;
}

// getopt_long implementation
int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longindex) {
  static int sp = 1;
  char *current_arg;

  // Reset if needed
  if (optind == 0) {
    optind = 1;
    sp = 1;
    reset_getopt();
  }

  optarg = NULL;

  if (optind >= argc) {
    return -1;
  }

  current_arg = argv[optind];

  // Check for end of options
  if (current_arg[0] != '-' || current_arg[1] == '\0') {
    return -1;
  }

  // Handle "--" (end of options)
  if (strcmp(current_arg, "--") == 0) {
    optind++;
    return -1;
  }

  // Handle long options
  if (current_arg[0] == '-' && current_arg[1] == '-') {
    const char *name_start = current_arg + 2;
    const char *name_end = strchr(name_start, '=');
    size_t name_len;

    if (name_end) {
      name_len = name_end - name_start;
    } else {
      name_len = strlen(name_start);
    }

    // Search for matching long option
    if (longopts != NULL) {
      for (int i = 0; longopts[i].name != NULL; i++) {
        if (strncmp(longopts[i].name, name_start, name_len) == 0 && longopts[i].name[name_len] == '\0') {
          // Found a match
          if (longindex != NULL) {
            *longindex = i;
          }

          // Handle argument
          if (longopts[i].has_arg == required_argument) {
            if (name_end) {
              optarg = (char *)(name_end + 1);
            } else if (optind + 1 < argc) {
              optarg = argv[++optind];
            } else {
              if (opterr && *optstring != ':') {
                fprintf(stderr, "%s: option '--%s' requires an argument\n", argv[0], longopts[i].name);
              }
              optind++;
              return (*optstring == ':') ? ':' : '?';
            }
          } else if (longopts[i].has_arg == optional_argument) {
            if (name_end) {
              optarg = (char *)(name_end + 1);
            }
          } else if (longopts[i].has_arg == no_argument && name_end) {
            if (opterr && *optstring != ':') {
              fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", argv[0], longopts[i].name);
            }
            optind++;
            return '?';
          }

          optind++;

          if (longopts[i].flag != NULL) {
            *longopts[i].flag = longopts[i].val;
            return 0;
          }

          return longopts[i].val;
        }
      }
    }

    // Unknown long option
    if (opterr && *optstring != ':') {
      fprintf(stderr, "%s: unrecognized option '--%.*s'\n", argv[0], (int)name_len, name_start);
    }
    optind++;
    return '?';
  }

  // Handle short options
  if (sp == 1) {
    sp++; // Skip the '-'
  }

  optopt = current_arg[sp];
  const char *oli = strchr(optstring, optopt);

  if (optopt == ':' || oli == NULL) {
    if (opterr && *optstring != ':') {
      fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], optopt);
    }
    if (current_arg[++sp] == '\0') {
      optind++;
      sp = 1;
    }
    return '?';
  }

  if (*(++oli) != ':') {
    // No argument
    optarg = NULL;
    if (current_arg[++sp] == '\0') {
      sp = 1;
      optind++;
    }
  } else {
    // Has argument
    if (*(oli + 1) == ':') {
      // Optional argument
      if (current_arg[sp + 1] != '\0') {
        optarg = &current_arg[sp + 1];
        optind++;
      } else {
        optarg = NULL;
        optind++;
      }
    } else {
      // Required argument
      if (current_arg[sp + 1] != '\0') {
        optarg = &current_arg[sp + 1];
        optind++;
      } else if (++optind >= argc) {
        if (*optstring == ':') {
          sp = 1;
          return ':';
        }
        if (opterr) {
          fprintf(stderr, "%s: option requires an argument -- '%c'\n", argv[0], optopt);
        }
        sp = 1;
        return '?';
      } else {
        optarg = argv[optind++];
      }
    }
    sp = 1;
  }

  return optopt;
}

#endif // _WIN32