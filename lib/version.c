// Embeds ASCII-Chat version information in the compiled binary
// This creates a custom ELF section readable with: readelf -p .ascii_chat_version <binary>

#include "version.h"

// GCC/Clang: Use __attribute__((section)) to place string in custom ELF section
// The "aSM" flags mean: 'a' = allocatable, 'S' = string section, 'M' = mergeable
#if defined(__GNUC__) || defined(__clang__)

// Just version in .ascii_chat_version section
__attribute__((used, section(".ascii_chat_version"))) const char ascii_chat_version_string[] = ASCII_CHAT_VERSION_FULL;

// Program name + version + OS in .comment section (visible in standard tooling)
__attribute__((used, section(".comment"))) const char ascii_chat_comment_string[] =
    "ascii-chat: " ASCII_CHAT_VERSION_FULL " (" ASCII_CHAT_OS ")";

// Custom section with all build info
__attribute__((used, section(".ascii_chat_comment"))) const char ascii_chat_build_info[] =
    "ascii-chat " ASCII_CHAT_VERSION_FULL " built on " ASCII_CHAT_OS " (" ASCII_CHAT_BUILD_TYPE ")";

#elif defined(_MSC_VER)

// MSVC: Use #pragma section and __declspec(allocate)
#pragma section(".rdata$ascii_chat_version", read)
__declspec(allocate(".rdata$ascii_chat_version")) const char ascii_chat_version_string[] =
    ASCII_CHAT_VERSION_FULL;

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
