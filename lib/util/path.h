#pragma once

/**
 * @file path.h
 * @brief Path manipulation utilities
 *
 * Provides utilities for working with file paths across platforms.
 */

#ifdef _WIN32
#define KNOWN_HOSTS_PATH "~\\.ascii-chat\\known_hosts"
#else
#define KNOWN_HOSTS_PATH "~/.ascii-chat/known_hosts"
#endif

/**
 * Extract relative path from an absolute path
 *
 * Searches for common project directories (lib/, src/, tests/, include/)
 * and returns the path relative from that directory.
 * Handles both Unix (/) and Windows (\) path separators.
 * Falls back to just the filename if no project directory found.
 *
 * @param file Absolute file path (typically from __FILE__)
 * @return Relative path from project directory (e.g., lib/platform/symbols.c),
 *         or filename if no project directory found
 */
const char *extract_project_relative_path(const char *file);

char *expand_path(const char *path);
