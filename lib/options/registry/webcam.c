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

#include "common.h"
#include "metadata.h"

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
     {.numeric_range = {0, 10, 1}, .examples = g_webcam_examples, .input_type = OPTION_INPUT_NUMERIC}},
    {"webcam-flip",
     'g',
     OPTION_TYPE_BOOL,
     offsetof(options_t, webcam_flip),
     &default_webcam_flip_value,
     sizeof(bool),
     "Flip webcam output horizontally before using it (press 'f' during rendering to toggle).",
     "WEBCAM",
     NULL,
     false,
     "ASCII_CHAT_WEBCAM_FLIP",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
    {"test-pattern",
     '\0',
     OPTION_TYPE_BOOL,
     offsetof(options_t, test_pattern),
     &default_test_pattern_value,
     sizeof(bool),
     "Use test pattern instead of webcam.",
     "WEBCAM",
     NULL,
     false,
     "WEBCAM_DISABLED",
     NULL,
     NULL,
     false,
     false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY,
     {0}},
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
     {0}},

    REGISTRY_TERMINATOR()};
