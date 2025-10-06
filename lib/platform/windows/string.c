/**
 * @file platform/windows/string.c
 * @brief Windows implementation of safe string functions
 *
 * This file provides Windows implementations of safe string functions
 * that satisfy clang-tidy cert-err33-c requirements.
 *
 * @author Assistant
 * @date December 2024
 */

#include "platform/string.h"
#include <stdarg.h>

// safe_snprintf and safe_fprintf are implemented in system.c
// to avoid duplicate symbol errors
