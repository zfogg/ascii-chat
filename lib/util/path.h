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
 * Searches for PROJECT_SOURCE_ROOT and returns the path relative to it.
 * Handles both Unix (/) and Windows (\) path separators.
 * Falls back to just the filename if source root not found.
 *
 * @param file Absolute file path (typically from __FILE__)
 * @return Relative path from project root, or filename if not found
 */
const char *extract_project_relative_path(const char *file);

char *expand_path(const char *path);
