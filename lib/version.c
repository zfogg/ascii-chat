/**
 * @file version.c
 * @ingroup version
 * @brief 🏷️ Binary-embedded version information in custom ELF/Mach-O sections for runtime inspection
 */

#include "version.h"

// GCC/Clang: Use __attribute__((section)) to place string in custom section
// ELF (Linux): .section_name format
// Mach-O (macOS): __SEGMENT,__section format
#if defined(__GNUC__) || defined(__clang__)

#if defined(__APPLE__)
// macOS Mach-O format: use __TEXT segment for read-only data
__attribute__((used, section("__TEXT,__ascii_chat"))) const char ascii_chat_custom_section[] =
    ASCII_CHAT_DOT_ASCII_CHAT_STRING;
__attribute__((used, section("__TEXT,__comment"))) const char ascii_chat_comment_string[] =
    ASCII_CHAT_DOT_COMMENT_STRING;
__attribute__((used, section("__TEXT,__version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;
#define ASCII_CHAT_VERSION_GETTERS

#elif defined(__linux__)
// Linux ELF format: standard .section_name format
__attribute__((used, section(".ascii_chat"))) const char ascii_chat_custom_section[] = ASCII_CHAT_DOT_ASCII_CHAT_STRING;
__attribute__((used, section(".comment"))) const char ascii_chat_comment_string[] = ASCII_CHAT_DOT_COMMENT_STRING;
__attribute__((used, section(".version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;
#define ASCII_CHAT_VERSION_GETTERS
#endif

#ifdef ASCII_CHAT_VERSION_GETTERS
// Provide functions to get version info at runtime
const char *ascii_chat_get_version(void) {
  return ascii_chat_version_string;
}
const char *ascii_chat_get_comment(void) {
  return ascii_chat_comment_string;
}
#endif

#endif
