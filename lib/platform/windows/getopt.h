#pragma once

#ifdef _WIN32

// Simple getopt implementation for Windows
// Based on public domain implementation

#ifdef __cplusplus
extern "C" {
#endif

// getopt variables
extern char *optarg;
extern int optind, opterr, optopt;

// Option structure for getopt_long
struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

// Argument requirements
#define no_argument 0
#define required_argument 1
#define optional_argument 2

// Function declarations
int getopt(int argc, char *const argv[], const char *optstring);
int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif // _WIN32