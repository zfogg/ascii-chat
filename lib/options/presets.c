/**
 * @file presets.c
 * @brief Preset option configurations for ascii-chat modes
 * @ingroup options
 */

#include "builder.h"
#include "options.h"
#include "parsers.h"
#include "actions.h"
#include "config.h"
#include "common.h"
#include "platform/terminal.h"
#include "video/palette.h"
#include "log/logging.h"
#include "registry.h"
#include "discovery/strings.h" // Added for dynamic session string generation

// ============================================================================
// NOTE: All static parser functions and group helper functions have been
// moved to lib/options/registry.c and are no longer needed here.
// The registry is now the single source of truth for all option definitions.
// ============================================================================

/**
 * @brief Add binary-level logging options to a builder
 *
 * @deprecated This function is deprecated. Use options_registry_add_all_to_builder() instead.
 * All option definitions are now centralized in the registry.
 */
__attribute__((deprecated("Use options_registry_add_all_to_builder instead"))) void
options_builder_add_logging_group(options_builder_t *b) {
  (void)b;
  // Kept for backward compatibility but no longer used
}

// ============================================================================
// Webcam & Display Options Helper (Client + Mirror)
// ============================================================================

// ============================================================================
// Unified Options Preset
// ============================================================================

/**
 * @brief Build unified options config with ALL options (binary + all modes)
 *
 * This is the single source of truth for all options. Each option has a
 * mode_bitmask indicating which modes it applies to. The config includes
 * all options, and validation happens after parsing based on detected mode.
 */
const options_config_t *options_preset_unified(const char *program_name, const char *description) {
  options_builder_t *b = options_builder_create(sizeof(options_t));
  if (!b) {
    SET_ERRNO(ERROR_MEMORY, "Failed to create options builder");
    return NULL;
  }

  b->program_name = program_name ? program_name : "ascii-chat";
  b->description = description ? description : "Video chat in your terminal";

  // Add ALL options from registry (binary + all modes)
  asciichat_error_t err = options_registry_add_all_to_builder(b);
  if (err != ASCIICHAT_OK) {
    options_builder_destroy(b);
    SET_ERRNO(err, "Failed to add all options to builder");
    return NULL;
  }

  // Generate random session strings for examples
  char session_buf1[SESSION_STRING_BUFFER_SIZE];
  char session_buf2[SESSION_STRING_BUFFER_SIZE];
  char session_buf3[SESSION_STRING_BUFFER_SIZE];
  char session_buf4[SESSION_STRING_BUFFER_SIZE];
  char session_buf5[SESSION_STRING_BUFFER_SIZE];
  char session_buf6[SESSION_STRING_BUFFER_SIZE];
  char session_buf7[SESSION_STRING_BUFFER_SIZE];
  char session_buf8[SESSION_STRING_BUFFER_SIZE];

  // Generate strings with fallbacks
  char *example_session_string = "bright-star-river";
  char *example_session_string2 = "happy-cloud-garden";
  char *example_session_string3 = "sweet-garden-haven";
  char *example_session_string4 = "vibrant-ocean-light";
  char *example_session_string5 = "bright-star-dream";
  char *example_session_string6 = "dream-path-moon";
  char *example_session_string7 = "lucky-light-ocean";
  char *example_session_string8 = "calm-river-cloud";

  // Try to generate all session strings
  if (acds_string_generate(session_buf1, sizeof(session_buf1)) == ASCIICHAT_OK) {
    example_session_string = session_buf1;
  }
  if (acds_string_generate(session_buf2, sizeof(session_buf2)) == ASCIICHAT_OK) {
    example_session_string2 = session_buf2;
  }
  if (acds_string_generate(session_buf3, sizeof(session_buf3)) == ASCIICHAT_OK) {
    example_session_string3 = session_buf3;
  }
  if (acds_string_generate(session_buf4, sizeof(session_buf4)) == ASCIICHAT_OK) {
    example_session_string4 = session_buf4;
  }
  if (acds_string_generate(session_buf5, sizeof(session_buf5)) == ASCIICHAT_OK) {
    example_session_string5 = session_buf5;
  }
  if (acds_string_generate(session_buf6, sizeof(session_buf6)) == ASCIICHAT_OK) {
    example_session_string6 = session_buf6;
  }
  if (acds_string_generate(session_buf7, sizeof(session_buf7)) == ASCIICHAT_OK) {
    example_session_string7 = session_buf7;
  }
  if (acds_string_generate(session_buf8, sizeof(session_buf8)) == ASCIICHAT_OK) {
    example_session_string8 = session_buf8;
  }

  // Add usage lines for all modes
  options_builder_add_usage(b, NULL, NULL, true, "Start a new session (share the session string)");
  options_builder_add_usage(b, NULL, "<session-string>", true, "Join an existing session");
  options_builder_add_usage(b, NULL, "<mode>", true, "Run in a specific mode");
  options_builder_add_usage(b, "server", "[bind-address] [bind-address]", true,
                            "Start server (can specify 0-2 bind addresses for IPv4/IPv6)");
  options_builder_add_usage(b, "client", "[address]", true, "Connect to server (defaults to localhost:27224)");
  options_builder_add_usage(b, "mirror", NULL, true, "View local webcam or media file as ASCII art");
  options_builder_add_usage(b, "discovery-service", "[bind-address] [bind-address]", true,
                            "Start discovery service (can specify 0-2 bind addresses for IPv4/IPv6)");

  // Add examples for binary-level help (implicitly discovery mode)
  options_builder_add_example(b, NULL, NULL, "Start new session (share the session string)", false);
  options_builder_add_example(b, NULL, example_session_string, "Join session with session string",
                              strcmp(example_session_string, "bright-star-river") != 0);
  options_builder_add_example(b, "server", NULL, "Run as dedicated server", false);
  options_builder_add_example(b, "client", "example.com", "Connect to specific server", false);
  options_builder_add_example(b, "mirror", NULL, "Preview local webcam as ASCII", false);

  // Add examples for server mode
  options_builder_add_example(b, "server", NULL, "Start on localhost (127.0.0.1 and ::1)", false);
  options_builder_add_example(b, "server", "0.0.0.0", "Start on all IPv4 interfaces", false);
  options_builder_add_example(b, "server", "0.0.0.0 ::", "Start on all IPv4 and IPv6 interfaces (dual-stack)", false);
  options_builder_add_example(b, "server", "--port 8080", "Start on custom port", false);
  options_builder_add_example(b, "server", "--key ~/.ssh/id_ed25519 --discovery",
                              "Start with identity key and discovery registration", false);

  // Add examples for client mode
  options_builder_add_example(b, "client", NULL, "Connect to localhost", false);
  options_builder_add_example(b, "client", "example.com", "Connect to remote server", false);
  options_builder_add_example(b, "client", "example.com:8080", "Connect to remote server on custom port", false);
  options_builder_add_example(b, "client", "--url 'https://www.youtube.com/watch?v=tQSbms5MDvY'",
                              "Stream from YouTube URL", false);
  options_builder_add_example(b, "client", "-f video.mp4", "Stream from local video file", false);
  options_builder_add_example(b, "client", "--color-mode mono --render-mode half-block --width 120",
                              "Connect with custom display options", false);
  options_builder_add_example(b, "client", "--palette-chars '@%#*+=-:. '", "Use custom ASCII palette characters",
                              false);
  options_builder_add_example(b, "client", "--snapshot", "Capture single frame and exit", false);

  // Add examples for mirror mode
  options_builder_add_example(b, "mirror", NULL, "View local webcam", false);
  options_builder_add_example(b, "mirror", "--color-mode mono", "View webcam in black and white", false);
  options_builder_add_example(b, "mirror", "--url 'https://www.youtube.com/watch?v=tQSbms5MDvY'",
                              "Stream from YouTube URL", false);
  options_builder_add_example(b, "mirror", "-f video.mp4",
                              "Stream from local video file (supports mp4, mkv, webm, mov, etc)", false);
  options_builder_add_example(b, "mirror", "-f '-'",
                              "Stream media from stdin (cat file.gif | ascii-chat mirror -f '-')", false);
  options_builder_add_example(b, "mirror", "--palette-chars '@%#*+=-:. '", "View with custom ASCII palette characters",
                              false);
  options_builder_add_example(b, "mirror", "--snapshot", "Capture single frame and exit", false);

  // Add examples for discovery-service mode
  options_builder_add_example(b, "discovery-service", NULL, "Start on localhost", false);
  options_builder_add_example(b, "discovery-service", "0.0.0.0", "Start on all IPv4 interfaces", false);
  options_builder_add_example(b, "discovery-service", "--port 5000", "Start on custom port", false);
  options_builder_add_example(b, "discovery-service", "--require-server-identity --require-client-identity",
                              "Enforce identity verification for all parties", false);

  // Add examples for discovery mode (shown for binary-level help as default)
  // Build session string examples dynamically
  char example_buf1[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf2[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf3[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf4[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf5[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf6[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];
  char example_buf7[SESSION_STRING_BUFFER_SIZE + BUFFER_SIZE_MEDIUM];

  snprintf(example_buf1, sizeof(example_buf1), "%s", example_session_string8);
  snprintf(example_buf2, sizeof(example_buf2), "%s --color", example_session_string2);
  snprintf(example_buf3, sizeof(example_buf3), "%s --discovery-server discovery.example.com", example_session_string3);
  snprintf(example_buf4, sizeof(example_buf4), "%s -f video.mp4", example_session_string4);
  snprintf(example_buf5, sizeof(example_buf5), "%s --url 'https://www.youtube.com/watch?v=tQSbms5MDvY'",
           example_session_string5);
  snprintf(example_buf6, sizeof(example_buf6), "%s -f '-'", example_session_string6);
  snprintf(example_buf7, sizeof(example_buf7), "%s --palette-chars '@%%#*+=-:. '", example_session_string7);

  options_builder_add_example(b, NULL, example_buf1, "Join a session using the session string", true);
  options_builder_add_example(b, NULL, example_buf2, "Join session with color support", true);
  options_builder_add_example(b, NULL, example_buf3, "Join session via custom discovery server", true);
  options_builder_add_example(b, NULL, example_buf4, "Join session and stream from local video file", true);
  options_builder_add_example(b, NULL, example_buf5, "Join session and stream from YouTube video", true);
  options_builder_add_example(b, NULL, example_buf6,
                              "Join session and stream media from stdin (cat file.mov | ascii-chat ... -f '-')", true);
  options_builder_add_example(b, NULL, example_buf7, "Join session with custom ASCII palette characters", true);

  // Add mode descriptions
  options_builder_add_mode(b, "server", "Run as multi-client video chat server");
  options_builder_add_mode(b, "client", "Run as video chat client (connect to server)");
  options_builder_add_mode(b, "mirror", "View local webcam as ASCII art (no server)");
  options_builder_add_mode(b, "discovery-service", "Secure P2P session signalling");

  // Add common dependencies (these will be validated after parsing)
  // Note: Dependencies are validated at runtime, so we add them here for documentation
  // URL conflicts: --url cannot be used with --file or --loop
  options_builder_add_dependency_conflicts(b, "url", "file",
                                           "Option --url cannot be used with --file (--url takes priority)");
  options_builder_add_dependency_conflicts(
      b, "url", "loop", "Option --url cannot be used with --loop (network streams cannot be looped)");
  // Encryption conflicts
  options_builder_add_dependency_conflicts(b, "no-encrypt", "encrypt", "Cannot use --no-encrypt with --encrypt");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "key", "Cannot use --no-encrypt with --key");
  options_builder_add_dependency_conflicts(b, "no-encrypt", "password", "Cannot use --no-encrypt with --password");
  // Compression conflicts
  options_builder_add_dependency_conflicts(b, "no-compress", "compression-level",
                                           "Cannot use --no-compress with --compression-level");
  // Audio encoding conflicts
  options_builder_add_dependency_conflicts(b, "encode-audio", "no-encode-audio",
                                           "Cannot use both --encode-audio and --no-encode-audio");
  // Cookies conflicts
  options_builder_add_dependency_conflicts(
      b, "cookies-from-browser", "no-cookies-from-browser",
      "--cookies-from-browser and --no-cookies-from-browser are mutually exclusive");
  options_builder_add_dependency_conflicts(
      b, "no-cookies-from-browser", "cookies-from-browser",
      "--no-cookies-from-browser and --cookies-from-browser are mutually exclusive");
  // Requirements
  options_builder_add_dependency_requires(b, "snapshot-delay", "snapshot",
                                          "Option --snapshot-delay requires --snapshot");
  options_builder_add_dependency_requires(b, "loop", "file", "Option --loop requires --file");

  const options_config_t *config = options_builder_build(b);
  options_builder_destroy(b);
  return config;
}
