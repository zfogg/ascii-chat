/**
 * @file util/uthash.h
 * @ingroup util
 * @brief Wrapper for uthash.h that ensures common.h is included first
 *
 * This header ensures that common.h is included before uthash.h throughout
 * the codebase. This is necessary because common.h defines HASH_FUNCTION
 * which must be set before uthash.h is included.
 *
 * @note Always include this instead of directly including uthash.h
 */

#pragma once

#include "../common.h"
#include "uthash.h"
