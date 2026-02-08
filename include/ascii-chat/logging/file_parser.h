#pragma once

/**
 * @file logging/file_parser.h
 * @brief Log file parsing and tailing utilities
 * @ingroup logging
 *
 * Provides functions to parse and tail log files for the interactive grep feature.
 * Handles log line format parsing and batch parsing from log files.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stddef.h>
#include <stdint.h>
#include <ascii-chat/common.h>
#include <ascii-chat/session/session_log_buffer.h>

/**
 * @brief Parse a single log line into a session_log_entry_t
 *
 * Expected format: `[TIMESTAMP] [LEVEL] message...`
 * Example: `[2026-02-08 12:34:56.789] [INFO] Server started on port 27224`
 *
 * @param line Log line to parse
 * @param out_entry Output entry (message field will be filled)
 * @return true if parsed successfully, false if line is empty/invalid
 */
bool log_file_parser_parse_line(const char *line, session_log_entry_t *out_entry);

/**
 * @brief Tail a log file and parse recent entries
 *
 * Reads the last max_size bytes from the file and parses them into entries.
 * This is efficient for large log files by avoiding full file reads.
 *
 * Entries are returned in chronological order (oldest first).
 *
 * @param file_path Path to log file to read
 * @param max_size Maximum bytes to read from end of file (e.g., 100*1024 for 100KB)
 * @param out_entries Output array to fill with parsed entries
 * @param max_count Maximum number of entries to return
 * @return Number of entries actually parsed, 0 on error
 *
 * @note Caller must free out_entries with SAFE_FREE
 * @note Returns partial entries gracefully (starts from next complete line)
 */
size_t log_file_parser_tail(const char *file_path, size_t max_size, session_log_entry_t **out_entries,
                            size_t max_count);

/**
 * @brief Merge and deduplicate log entries from two sources
 *
 * Combines entries from log file and in-memory buffer into a single sorted array,
 * removing duplicates based on sequence number or exact message match.
 *
 * Result is sorted chronologically (oldest first).
 *
 * @param buffer_entries Entries from in-memory buffer
 * @param buffer_count Number of buffer entries
 * @param file_entries Entries parsed from log file
 * @param file_count Number of file entries
 * @param out_merged Output array with merged entries (allocated by function)
 * @return Number of merged entries, or 0 on error
 *
 * @note Caller must free out_merged with SAFE_FREE
 */
size_t log_file_parser_merge_and_dedupe(const session_log_entry_t *buffer_entries, size_t buffer_count,
                                        const session_log_entry_t *file_entries, size_t file_count,
                                        session_log_entry_t **out_merged);
