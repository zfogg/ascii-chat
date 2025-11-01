/**
 * @file symbols.c (POSIX)
 * @brief Symbol resolution - delegates to cross-platform implementation
 *
 * The symbol resolution implementation is now cross-platform and lives in
 * ../symbols.c. This file just includes it for POSIX builds.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2025
 */

// The implementation in symbols.c is cross-platform and handles
// both Windows (.exe binaries) and POSIX (Unix binaries) via #ifdef _WIN32
#include "../symbols.c"
