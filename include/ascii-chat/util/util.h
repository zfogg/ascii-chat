#pragma once

/**
 * @file util/util.h
 * @brief 🛠️ Master Utility Header
 * @ingroup util
 *
 * This is a convenience header that includes all utility modules. Including
 * this header provides access to all utility functions in a single include.
 *
 * INCLUDED MODULES:
 * =================
 * This header includes:
 * - aspect_ratio.h: Aspect ratio calculation functions
 * - bytes.h: Byte-level access and safe arithmetic utilities
 * - format.h: String formatting utilities
 * - ip.h: IP address parsing and formatting
 * - math.h: Mathematical utilities (rounding, clamping, power-of-two)
 * - parsing.h: Protocol message parsing utilities
 * - path.h: Path manipulation utilities
 * - string.h: String manipulation and shell escaping
 * - utf8.h: UTF-8 encoding and decoding utilities
 * - fnv1a.h: FNV-1a hash function implementation
 * - thread.h: Thread management helper macros
 * - atomic.h: Atomic operations convenience macros
 * - endian.h: Network byte order conversion helpers
 *
 * USAGE:
 * ======
 * Instead of including individual utility headers:
 * @code
 * #include "util/path.h"
 * #include "util/ip.h"
 * #include "util/format.h"
 * @endcode
 *
 * Include this master header:
 * @code
 * #include "util/util.h"
 * @endcode
 *
 * @note This header includes all utility modules, so it may bring in more
 *       dependencies than needed. For fine-grained control, include individual
 *       headers instead.
 * @note All included headers are in the @ref util group.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/util/bytes.h>
#include <ascii-chat/util/fnv1a.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/math.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/thread.h>
#include <ascii-chat/util/atomic.h>
#include <ascii-chat/util/endian.h>
