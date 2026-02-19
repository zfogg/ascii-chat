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
    log_debug("Failed to open /proc/self/maps");
    return 0;
  }

  char line[512];
  while (fgets(line, sizeof(line), maps) && count < max_matches) {
    uintptr_t start, end, offset;
    char perms[5], device[10], path[PLATFORM_MAX_PATH_LENGTH];
    unsigned long inode;
    int path_start;

    // Parse: start-end perms offset device inode path
    // Example: 7f3a2b1c0000-7f3a2b1c1000 r-xp 00000000 08:02 12345678   /usr/lib/libsodium.so.23
    int parsed = sscanf(line, "%lx-%lx %4s %lx %9s %lu", &start, &end, perms, &offset, device, &inode);

    if (parsed < 6) {
      continue; // Incomplete line
    }

    // Skip lines without executable flag
    if (perms[2] != 'x') {
      continue;
    }

    // Extract path (everything after inode, skip whitespace)
    path_start = 0;
    for (int i = 0; i < (int)strlen(line); i++) {
      if (line[i] != ' ' && line[i] != '\t')
        continue;
      int space_count = 0;
      int j = i;
      while (j < (int)strlen(line) && (line[j] == ' ' || line[j] == '\t')) {
        space_count++;
        j++;
      }
      if (space_count >= 2 && j < (int)strlen(line)) { // Found field separator with multiple spaces
        if (j > 50) {                                  // Likely past inode field
          path_start = j;
          break;
        }
      }
    }

    if (path_start == 0 || path_start >= (int)strlen(line)) {
      continue; // No path found
    }

    // Extract path and remove newline
    strncpy(path, &line[path_start], PLATFORM_MAX_PATH_LENGTH - 1);
    path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
    size_t path_len = strlen(path);
    if (path_len > 0 && path[path_len - 1] == '\n') {
      path[path_len - 1] = '\0';
    }

    // Check if address falls within this segment
    if (addr_int >= start && addr_int < end) {
      strncpy(matches[count].path, path, PLATFORM_MAX_PATH_LENGTH - 1);
      matches[count].path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
      matches[count].file_offset = (addr_int - start) + offset;

#ifndef NDEBUG
      log_debug("[Linux /proc/self/maps] addr=%p matches %s (offset=%lx)", addr, path, matches[count].file_offset);
#endif
      count++;
    }
  }

  fclose(maps);
  return count;
}
