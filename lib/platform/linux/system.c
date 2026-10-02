/**
 * @file platform/linux/system.c
 * @ingroup platform
 * @brief 🐧 Linux system utilities and backtrace symbol resolution
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/internal.h>
#include <ascii-chat/common.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief Get binary that contains address on Linux via /proc/self/maps
 *
 * Scans /proc/self/maps to find which loaded binary (exe or .so) contains
 * the given runtime address. Returns the file offset within that binary,
 * which is passed to llvm-symbolizer for symbol resolution.
 *
 * @param addr Runtime address from backtrace
 * @param matches Output array for matches (path, offset, is_project_lib)
 * @param max_matches Maximum number of matches to store
 * @return Number of matches found (0, 1, or rarely 2)
 */
int get_binary_file_address_offsets(const void *addr, platform_binary_match_t *matches, int max_matches) {
  int count = 0;
  uintptr_t addr_int = (uintptr_t)addr;

  FILE *maps = fopen("/proc/self/maps", "r");
  if (!maps) {
    return 0;
  }

  char line[512];
  while (fgets(line, sizeof(line), maps) && count < max_matches) {
    char perms[5], device[10], path[PLATFORM_MAX_PATH_LENGTH];

    // Parse: start-end perms offset device inode path
    // Example: 7f3a2b1c0000-7f3a2b1c1000 r-xp 00000000 08:02 12345678   /usr/lib/libsodium.so.23
    char *p = line;
    char *end_ptr;

    uintptr_t start = (uintptr_t)strtoul(p, &end_ptr, 16);
    if (end_ptr == p || *end_ptr != '-') {
      continue;
    }
    p = end_ptr + 1;

    uintptr_t end = (uintptr_t)strtoul(p, &end_ptr, 16);
    if (end_ptr == p || *end_ptr != ' ') {
      continue;
    }
    p = end_ptr + 1;

    // Parse perms field (4 chars like "r-xp")
    if (strlen(p) < 4) {
      continue;
    }
    memcpy(perms, p, 4);
    perms[4] = '\0';
    p += 4;
    if (*p != ' ') {
      continue;
    }
    p++;

    uintptr_t offset = (uintptr_t)strtoul(p, &end_ptr, 16);
    if (end_ptr == p || *end_ptr != ' ') {
      continue;
    }
    p = end_ptr + 1;

    // Parse device field (like "08:02")
    char *space = strchr(p, ' ');
    if (!space || (size_t)(space - p) >= sizeof(device)) {
      continue;
    }
    memcpy(device, p, (size_t)(space - p));
    device[space - p] = '\0';
    p = space + 1;

    // Skip whitespace and inode field
    while (*p == ' ') p++;
    strtoul(p, &end_ptr, 10);
    if (end_ptr == p) {
      continue;
    }

    // Skip lines without executable flag
    if (perms[2] != 'x') {
      continue;
    }

    // Extract path more robustly by searching for the last whitespace-separated token
    // /proc/self/maps format: start-end perms offset device inode [path]
    // The path is optional and starts after the inode field
    path[0] = '\0';

    // Find the last whitespace and take everything after it as the path
    // This is more robust than trying to count fields
    int line_len = strlen(line);
    int path_start = -1;

    // Scan from the end backwards to find the last non-whitespace character
    for (int i = line_len - 1; i >= 0; i--) {
      if (line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && line[i] != '\r') {
        // Found a non-whitespace char, now find the start of this token
        path_start = i;
        while (path_start > 0 && line[path_start - 1] != ' ' && line[path_start - 1] != '\t') {
          path_start--;
        }
        break;
      }
    }

    if (path_start > 0 && line[path_start - 1] != '\0') {
      // Extract the path starting from path_start
      strncpy(path, &line[path_start], PLATFORM_MAX_PATH_LENGTH - 1);
      path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
      // Remove trailing whitespace
      int path_len = strlen(path);
      while (path_len > 0 && (path[path_len - 1] == '\n' || path[path_len - 1] == '\r')) {
        path[--path_len] = '\0';
      }
    }

    // Check if address falls within this segment
    if (perms[2] == 'x' && path[0] == '/') {
      if (addr_int >= start && addr_int < end) {
        strncpy(matches[count].path, path, PLATFORM_MAX_PATH_LENGTH - 1);
        matches[count].path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
        matches[count].file_offset = (addr_int - start) + offset;
        count++;
      }
    }
  }

  fclose(maps);
  return count;
}
