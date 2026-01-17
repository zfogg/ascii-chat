/**
 * @file actions.c
 * @brief Action option callbacks for ascii-chat
 * @ingroup options
 *
 * Action options execute immediately during parsing and may exit the program.
 * Examples: --list-webcams, --version, --show-capabilities
 */

#include "actions.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/server.h"
#include "options/client.h"
#include "options/mirror.h"
#include "platform/terminal.h"
#include "version.h"
#include "video/webcam/webcam.h"
#include "audio/audio.h"

#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Webcam Action
// ============================================================================

void action_list_webcams(void) {
  webcam_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = webcam_list_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Error: Failed to enumerate webcam devices\n");
    _exit(1);
  }

  if (device_count == 0) {
    (void)fprintf(stdout, "No webcam devices found.\n");
  } else {
    (void)fprintf(stdout, "Available webcam devices:\n");
    for (unsigned int i = 0; i < device_count; i++) {
      (void)fprintf(stdout, "  %u: %s\n", devices[i].index, devices[i].name);
    }
  }

  webcam_free_device_list(devices);
  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Audio Device Actions
// ============================================================================

void action_list_microphones(void) {
  audio_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = audio_list_input_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Error: Failed to enumerate audio input devices\n");
    _exit(1);
  }

  if (device_count == 0) {
    (void)fprintf(stdout, "No microphone devices found.\n");
  } else {
    (void)fprintf(stdout, "Available microphone devices:\n");
    for (unsigned int i = 0; i < device_count; i++) {
      (void)fprintf(stdout, "  %d: %s", devices[i].index, devices[i].name);
      if (devices[i].is_default_input) {
        (void)fprintf(stdout, " (default)");
      }
      (void)fprintf(stdout, "\n");
    }
  }

  audio_free_device_list(devices);
  (void)fflush(stdout);
  _exit(0);
}

void action_list_speakers(void) {
  audio_device_info_t *devices = NULL;
  unsigned int device_count = 0;

  asciichat_error_t result = audio_list_output_devices(&devices, &device_count);
  if (result != ASCIICHAT_OK) {
    (void)fprintf(stderr, "Error: Failed to enumerate audio output devices\n");
    _exit(1);
  }

  if (device_count == 0) {
    (void)fprintf(stdout, "No speaker devices found.\n");
  } else {
    (void)fprintf(stdout, "Available speaker devices:\n");
    for (unsigned int i = 0; i < device_count; i++) {
      (void)fprintf(stdout, "  %d: %s", devices[i].index, devices[i].name);
      if (devices[i].is_default_output) {
        (void)fprintf(stdout, " (default)");
      }
      (void)fprintf(stdout, "\n");
    }
  }

  audio_free_device_list(devices);
  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Terminal Capabilities Action
// ============================================================================

void action_show_capabilities(void) {
  terminal_capabilities_t caps = detect_terminal_capabilities();

  (void)fprintf(stdout, "Terminal Capabilities:\n");
  (void)fprintf(stdout, "  Color Support: ");
  switch (caps.color_level) {
  case TERM_COLOR_NONE:
    (void)fprintf(stdout, "None (monochrome)\n");
    break;
  case TERM_COLOR_16:
    (void)fprintf(stdout, "16 colors (ANSI)\n");
    break;
  case TERM_COLOR_256:
    (void)fprintf(stdout, "256 colors\n");
    break;
  case TERM_COLOR_TRUECOLOR:
    (void)fprintf(stdout, "Truecolor (16.7M colors)\n");
    break;
  default:
    (void)fprintf(stdout, "Unknown\n");
    break;
  }

  (void)fprintf(stdout, "  UTF-8 Support: %s\n", caps.utf8_support ? "Yes" : "No");
  (void)fprintf(stdout, "  Terminal Type: %s\n", caps.term_type[0] ? caps.term_type : "Unknown");
  (void)fprintf(stdout, "  Color Term: %s\n", caps.colorterm[0] ? caps.colorterm : "Not set");

  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Version Action
// ============================================================================

void action_show_version(void) {
  (void)fprintf(stdout, "ascii-chat %s (%s, %s)\n", ASCII_CHAT_VERSION_FULL, ASCII_CHAT_BUILD_TYPE,
                ASCII_CHAT_BUILD_DATE);
  (void)fprintf(stdout, "\n");
  (void)fprintf(stdout, "Built with:\n");

#ifdef __clang__
  (void)fprintf(stdout, "  Compiler: Clang %s\n", __clang_version__);
#elif defined(__GNUC__)
  (void)fprintf(stdout, "  Compiler: GCC %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  (void)fprintf(stdout, "  Compiler: MSVC %d\n", _MSC_VER);
#else
  (void)fprintf(stdout, "  Compiler: Unknown\n");
#endif

#ifdef USE_MUSL
  (void)fprintf(stdout, "  C Library: musl\n");
#elif defined(__GLIBC__)
  (void)fprintf(stdout, "  C Library: glibc %d.%d\n", __GLIBC__, __GLIBC_MINOR__);
#elif defined(_WIN32)
  (void)fprintf(stdout, "  C Library: MSVCRT\n");
#elif defined(__APPLE__)
  (void)fprintf(stdout, "  C Library: libSystem\n");
#else
  (void)fprintf(stdout, "  C Library: Unknown\n");
#endif

  (void)fprintf(stdout, "\n");
  (void)fprintf(stdout, "For more information: https://github.com/zfogg/ascii-chat\n");

  (void)fflush(stdout);
  _exit(0);
}

// ============================================================================
// Help Actions
// ============================================================================

void action_help_server(void) {
  usage_server(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_client(void) {
  usage_client(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_mirror(void) {
  usage_mirror(stdout);
  (void)fflush(stdout);
  _exit(0);
}

void action_help_acds(void) {
  usage_acds(stdout);
  (void)fflush(stdout);
  _exit(0);
}
