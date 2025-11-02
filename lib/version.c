// Embeds ASCII-Chat version information in the compiled binary
// This creates a custom ELF section readable with: readelf -p .ascii_chat_version <binary>

#include "version.h"

// GCC/Clang: Use __attribute__((section)) to place string in custom section
// ELF (Linux): .section_name format
// Mach-O (macOS): __SEGMENT,__section format
#if defined(__GNUC__) || defined(__clang__)

#ifdef __APPLE__
// macOS Mach-O format: use __TEXT segment for read-only data
__attribute__((used, section("__TEXT,__version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;

// macOS doesn't have .comment section, use __TEXT,__info_plist or custom section
__attribute__((used, section("__TEXT,__comment"))) const char ascii_chat_comment_string[] =
    "ascii-chat: " ASCII_CHAT_VERSION_FULL " (" ASCII_CHAT_OS ")";

__attribute__((used, section("__TEXT,__build_info"))) const char ascii_chat_build_info[] =
    "ascii-chat " ASCII_CHAT_VERSION_FULL " built on " ASCII_CHAT_OS " (" ASCII_CHAT_BUILD_TYPE ")";
#else
// Linux ELF format: standard .section_name format
__attribute__((used, section(".ascii_chat_version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;

// Program name + version + OS in .comment section (visible in standard tooling)
__attribute__((used, section(".comment"))) const char ascii_chat_comment_string[] =
    "ascii-chat: " ASCII_CHAT_VERSION_FULL " (" ASCII_CHAT_OS ")";

// Custom section with all build info
__attribute__((used, section(".ascii_chat_comment"))) const char ascii_chat_build_info[] =
    "ascii-chat " ASCII_CHAT_VERSION_FULL " built on " ASCII_CHAT_OS " (" ASCII_CHAT_BUILD_TYPE ")";
#endif

#elif defined(_MSC_VER)

// MSVC: Use #pragma section and __declspec(allocate)
#pragma section(".rdata$ascii_chat_version", read)
__declspec(allocate(".rdata$ascii_chat_version")) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;

#pragma section(".rdata$ascii_chat_comment", read)
__declspec(allocate(".rdata$ascii_chat_comment")) const char ascii_chat_build_info[] =
    "ascii-chat " ASCII_CHAT_VERSION_FULL " built on " ASCII_CHAT_OS " (" ASCII_CHAT_BUILD_TYPE ")";

#endif

// Provide functions to get version info at runtime
const char *ascii_chat_get_version(void) {
  return ascii_chat_version_string;
}

const char *ascii_chat_get_comment(void) {
#if defined(__GNUC__) || defined(__clang__)
  return ascii_chat_comment_string;
#else
  return ascii_chat_build_info;
#endif
}

const char *ascii_chat_get_build_info(void) {
  return ascii_chat_build_info;
}
