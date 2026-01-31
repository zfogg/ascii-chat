/**
 * @file actions.h
 * @brief Action option callbacks for ascii-chat
 * @ingroup options
 */

#pragma once

/**
 * @brief List available webcam devices and exit
 *
 * Enumerates all webcam devices and prints them to stdout.
 * Exits with code 0 on success, 1 on error.
 */
void action_list_webcams(void);

/**
 * @brief List available microphone devices and exit
 *
 * Enumerates all audio input devices and prints them to stdout.
 * Exits with code 0 on success, 1 on error.
 */
void action_list_microphones(void);

/**
 * @brief List available speaker devices and exit
 *
 * Enumerates all audio output devices and prints them to stdout.
 * Exits with code 0 on success, 1 on error.
 */
void action_list_speakers(void);

/**
 * @brief Show terminal capabilities and exit
 *
 * Detects and displays terminal color support, UTF-8 support,
 * dimensions, and terminal program name.
 * Exits with code 0.
 */
void action_show_capabilities(void);

/**
 * @brief Show version information and exit
 *
 * Displays ascii-chat version, build type, build date, and
 * compiler information.
 * Exits with code 0.
 */
void action_show_version(void);

/**
 * @brief Show server mode help and exit
 *
 * Displays server mode usage and options.
 * Exits with code 0.
 */
void action_help_server(void);

/**
 * @brief Show client mode help and exit
 *
 * Displays client mode usage and options.
 * Exits with code 0.
 */
void action_help_client(void);

/**
 * @brief Show mirror mode help and exit
 *
 * Displays mirror mode usage and options.
 * Exits with code 0.
 */
void action_help_mirror(void);

/**
 * @brief Show discovery-service mode help and exit
 *
 * Displays discovery-service mode usage and options.
 * Exits with code 0.
 */
void action_help_acds(void);

/**
 * @brief Show discovery mode help and exit
 *
 * Displays discovery mode usage and options.
 * Exits with code 0.
 */
void action_help_discovery(void);

/**
 * @brief Generate man page template from options builder
 *
 * Generates a merged man page template preserving manual content.
 * Outputs to stdout by default, or to specified file path if provided.
 * Prompts for overwrite confirmation if file already exists.
 * Exits with code 0 on success, 1 on error.
 *
 * @param output_path Optional file path to write man page to. NULL or empty string = stdout.
 */
void action_create_manpage(const char *output_path);

/**
 * @brief Create default configuration file and exit
 *
 * Creates a default configuration file. Outputs to stdout by default,
 * or to specified file path if provided.
 * Prompts for overwrite confirmation if file already exists.
 * Exits with code 0 on success, 1 on error.
 *
 * @param output_path Optional file path to write config to. NULL or empty string = stdout.
 */
void action_create_config(const char *output_path);

/**
 * @brief Generate shell completions and output to stdout or file
 *
 * Generates shell completion script for the specified shell (bash, fish, zsh, powershell).
 * Output is dynamically generated from the options registry, ensuring completions are
 * always in sync with current options.
 * Outputs to stdout by default, or to specified file path if provided.
 * Prompts for overwrite confirmation if file already exists.
 * Exits with code 0 on success, 1 on error.
 *
 * @param shell_name Shell name: "bash", "fish", "zsh", or "powershell"
 * @param output_path Optional file path to write completions to. NULL or empty string = stdout.
 */
void action_completions(const char *shell_name, const char *output_path);
