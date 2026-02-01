/**
 * @file actions.h
 * @brief Action option callbacks for ascii-chat
 * @ingroup options
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Action type enumeration
 *
 * Defines all possible deferred actions that can be executed after
 * options are fully parsed and initialized.
 */
typedef enum {
  ACTION_NONE = 0,
  ACTION_LIST_WEBCAMS,
  ACTION_LIST_MICROPHONES,
  ACTION_LIST_SPEAKERS,
  ACTION_SHOW_CAPABILITIES,
} deferred_action_t;

/**
 * @brief Action argument structure
 *
 * Holds optional arguments for actions that need them (e.g., config path for --config-create)
 */
typedef struct {
  const char *output_path; ///< Output path for generation actions (config, manpage, completions)
  const char *shell_name;  ///< Shell name for completion actions
} action_args_t;

/**
 * @brief Set which action to defer until options are fully initialized
 *
 * Only the first action found is remembered. If multiple actions are specified,
 * only the first one will be executed.
 *
 * @param action The action type to defer
 * @param args Optional action arguments (can be NULL)
 */
void actions_defer(deferred_action_t action, const action_args_t *args);

/**
 * @brief Get the deferred action (if any) that was set
 *
 * @return The deferred action type, or ACTION_NONE if no action was deferred
 */
deferred_action_t actions_get_deferred(void);

/**
 * @brief Get the arguments for the deferred action
 *
 * @return Pointer to action arguments, or NULL if no arguments were set
 */
const action_args_t *actions_get_args(void);

/**
 * @brief Execute the deferred action (if any)
 *
 * This function should be called during STAGE 8 of options_init()
 * after all options are fully parsed and dimensions are calculated.
 */
void actions_execute_deferred(void);

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
 *
 * Note: This action is deferred until after all options are parsed
 * and dimensions are calculated, so that --width and --height flags
 * are properly reflected in the output.
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
