// Embeds ASCII-Chat version information in the compiled binary
// This creates a custom ELF section readable with: readelf -p .ascii_chat_version <binary>

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

#elif defined(_MSC_VER)
// MSVC: Use #pragma section and __declspec(allocate)
#pragma section(".rdata$ascii_chat", read)
__declspec(allocate(".rdata$ascii_chat")) const char ascii_chat_custom_section[] = ASCII_CHAT_DOT_ASCII_CHAT_STRING;
#pragma section(".rdata$comment", read)
__declspec(allocate(".rdata$comment")) const char ascii_chat_comment_string[] = ASCII_CHAT_DOT_COMMENT_STRING;
#pragma section(".rdata$version", read)
__declspec(allocate(".rdata$version")) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;
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
