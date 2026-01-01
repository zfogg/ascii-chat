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
