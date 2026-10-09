/**
 * @file webcam.c
 * @brief Webcam capture options
 * @ingroup options
 *
 * Options for selecting and configuring webcam devices, test patterns,
 * and device listing.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/registry/metadata.h>
#include <string.h>

static bool parse_test_pattern(const char *arg, void *dest, char **error_msg) {
  if (arg && strcmp(arg, "0") != 0 && strcmp(arg, "1") != 0) {
    *error_msg = platform_strdup("Test pattern must be 0 or 1");
    return false;
  }
  options_t *opts = (options_t *)((char *)dest - offsetof(options_t, test_pattern));
  opts->test_pattern = true;
  opts->test_pattern_index = arg && arg[0] == '1' ? 1 : 0;
  return true;
}

static const char *const pattern_values[] = {"0", "1", NULL};

// ============================================================================
// WEBCAM CATEGORY - Webcam capture options
// ============================================================================
const registry_entry_t g_webcam_entries[] = {
    // WEBCAM GROUP (client, mirror, discovery)
    {"webcam-index",
     'c',
     OPTION_TYPE_INT,
     offsetof(options_t, webcam_index),
     &default_webcam_index_value,
     sizeof(unsigned short int),
     "Webcam device index to use for video input.",
     "WEBCAM",
     NULL,
     false,
     "ASCII_CHAT_WEBCAM_INDEX",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.numeric_range = {0, 10, 1}, .examples = g_webcam_examples, .input_type = OPTION_INPUT_NUMERIC},
     NULL},
    {"test-pattern",
     '\0',
     OPTION_TYPE_CALLBACK,
     offsetof(options_t, test_pattern),
     NULL,
     sizeof(bool),
     "Use synthetic video: 0 = gradient and square (default), 1 = rainbow and circle.",
     "WEBCAM",
     "[0|1]",
     false,
     "WEBCAM_DISABLED",
     NULL,
     parse_test_pattern,
     false,
     true,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {.enum_values = pattern_values, .input_type = OPTION_INPUT_ENUM},
     NULL},
    {"list-webcams",
     '\0',
     OPTION_TYPE_ACTION,
     0,
     NULL,
     0,
     "List available webcam devices by index and exit.",
     "WEBCAM",
     NULL,
     false,
     NULL,
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_BINARY,
     {0},
     NULL},

    REGISTRY_TERMINATOR()};
