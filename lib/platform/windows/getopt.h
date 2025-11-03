#pragma once

#ifdef _WIN32

/**
 * @file platform/windows/getopt.h
 * @ingroup platform
 * @brief POSIX getopt implementation for Windows
 *
 * This header provides a POSIX-compatible getopt and getopt_long implementation
 * for Windows systems. Windows does not provide these functions by default, so
 * this implementation fills the gap to enable cross-platform command-line
 * argument parsing.
 *
 * CORE FEATURES:
 * ==============
 * - POSIX getopt() function for short option parsing
 * - POSIX getopt_long() function for long option parsing
 * - Standard getopt variables (optarg, optind, opterr, optopt)
 * - Standard option argument requirements (no_argument, required_argument, optional_argument)
 * - Compatible with POSIX getopt behavior
 *
 * WINDOWS COMPATIBILITY:
 * ======================
 * This implementation provides full compatibility with POSIX getopt behavior:
 * - Short options with single dash: -a -b -c
 * - Long options with double dash: --help --version
 * - Required and optional arguments
 * - Option grouping: -abc is equivalent to -a -b -c
 * - Unknown option handling
 *
 * USAGE:
 * =====
 * @code
 * // Short options
 * while ((opt = getopt(argc, argv, "hv:")) != -1) {
 *   switch (opt) {
 *     case 'h': printf("Help\n"); break;
 *     case 'v': printf("Version: %s\n", optarg); break;
 *     case '?': return 1;
 *   }
 * }
 *
 * // Long options
 * static struct option long_options[] = {
 *   {"help", no_argument, 0, 'h'},
 *   {"version", required_argument, 0, 'v'},
 *   {0, 0, 0, 0}
 * };
 *
 * while ((opt = getopt_long(argc, argv, "hv:", long_options, NULL)) != -1) {
 *   switch (opt) {
 *     case 'h': printf("Help\n"); break;
 *     case 'v': printf("Version: %s\n", optarg); break;
 *     case '?': return 1;
 *   }
 * }
 * @endcode
 *
 * @note This header is only included on Windows builds. POSIX systems
 *       should use the standard <getopt.h> header.
 * @note Based on a public domain implementation for maximum compatibility.
 * @note All getopt variables (optarg, optind, opterr, optopt) are declared
 *       as extern and should be linked from the platform implementation.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

// Simple getopt implementation for Windows
// Based on public domain implementation

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Getopt Variables
 * @ingroup platform
 * @{
 */

/** @brief Pointer to argument of current option (for options requiring arguments) */
extern char *optarg;

/** @brief Index of next argument to process */
extern int optind;

/** @brief Error flag (non-zero to print errors, zero to suppress) */
extern int opterr;

/** @brief Current option character being processed */
extern int optopt;

/** @} */

/**
 * @brief Option structure for getopt_long()
 *
 * Used to define long options (e.g., --help, --version) for getopt_long().
 *
 * @ingroup platform
 */
struct option {
  const char *name; /**< Long option name (without -- prefix) */
  int has_arg;      /**< Argument requirement (no_argument, required_argument, optional_argument) */
  int *flag;        /**< If non-NULL, set *flag to val and return 0; otherwise return val */
  int val;          /**< Return value when flag is NULL */
};

/**
 * @name Option Argument Requirements
 * @ingroup platform
 * @{
 */

/** @brief Option takes no argument */
#define no_argument 0

/** @brief Option requires an argument */
#define required_argument 1

/** @brief Option accepts an optional argument */
#define optional_argument 2

/** @} */

/**
 * @brief Parse command-line options (POSIX getopt)
 * @param argc Argument count
 * @param argv Argument vector
 * @param optstring String of option characters (e.g., "hv:" means -h, -v with argument)
 * @return Option character, '?' for unknown option, ':' for missing argument, -1 when done
 *
 * Parses command-line arguments for short options. Options are single characters
 * preceded by a single dash (e.g., -h, -v). Options requiring arguments are followed
 * by a colon in optstring (e.g., "v:" means -v requires an argument).
 *
 * @ingroup platform
 *
 * @par Example
 * @code
 * // Parse -h (help) and -v (version with argument)
 * while ((opt = getopt(argc, argv, "hv:")) != -1) {
 *   switch (opt) {
 *     case 'h': show_help(); break;
 *     case 'v': printf("Version: %s\n", optarg); break;
 *     case '?': return 1; // Unknown option
 *   }
 * }
 * @endcode
 */
int getopt(int argc, char *const argv[], const char *optstring);

/**
 * @brief Parse command-line options including long options (POSIX getopt_long)
 * @param argc Argument count
 * @param argv Argument vector
 * @param optstring String of option characters for short options
 * @param longopts Array of struct option defining long options (terminated with {0,0,0,0})
 * @param longindex If non-NULL, set to index of matched long option
 * @return Option character (from optstring or longopts), '?' for unknown option, -1 when done
 *
 * Parses both short options (e.g., -h) and long options (e.g., --help). Short options
 * are parsed the same way as getopt(). Long options are matched from the longopts array.
 *
 * @ingroup platform
 *
 * @par Example
 * @code
 * static struct option long_options[] = {
 *   {"help", no_argument, 0, 'h'},
 *   {"version", required_argument, 0, 'v'},
 *   {0, 0, 0, 0}
 * };
 *
 * while ((opt = getopt_long(argc, argv, "hv:", long_options, NULL)) != -1) {
 *   switch (opt) {
 *     case 'h': show_help(); break;
 *     case 'v': printf("Version: %s\n", optarg); break;
 *   }
 * }
 * @endcode
 */
int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longindex);

#ifdef __cplusplus
}
#endif

#endif // _WIN32
