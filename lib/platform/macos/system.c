/**
 * @file platform/macos/system.c
 * @ingroup platform
 * @brief 🍎 macOS system utilities and backtrace symbol resolution
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/internal.h>
#include <ascii-chat/common.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <string.h>

/**
 * @brief Get binary that contains address on macOS via dyld
 *
 * Iterates through dyld-loaded images to find which one contains
 * the given runtime address. Returns its preferred virtual address with
 * the dyld ASLR slide removed, which llvm-symbolizer expects for Mach-O.
 *
 * @param addr Runtime address from backtrace
 * @param matches Output array for matches (path, offset, is_project_lib)
 * @param max_matches Maximum number of matches to store
 * @return Number of matches found (0, 1, or rarely 2)
 */
int get_binary_file_address_offsets(const void *addr, platform_binary_match_t *matches, int max_matches) {
  int count = 0;
  uintptr_t addr_int = (uintptr_t)addr;

  uint32_t image_count = _dyld_image_count();
  for (uint32_t i = 0; i < image_count && count < max_matches; i++) {
    const char *image_name = _dyld_get_image_name(i);
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(i);
    intptr_t slide = _dyld_get_image_vmaddr_slide(i);

    if (!header || !image_name) {
      continue;
    }

    if (header->magic != MH_MAGIC_64) {
      continue;
    }

    const struct load_command *cmd = (const struct load_command *)(header + 1);
    for (uint32_t command_index = 0; command_index < header->ncmds; command_index++) {
      if (cmd->cmd == LC_SEGMENT_64) {
        const struct segment_command_64 *segment = (const struct segment_command_64 *)cmd;
        uintptr_t loaded_start = (uintptr_t)(segment->vmaddr + slide);
        uintptr_t loaded_end = loaded_start + (uintptr_t)segment->vmsize;

        if (addr_int >= loaded_start && addr_int < loaded_end) {
          strncpy(matches[count].path, image_name, PLATFORM_MAX_PATH_LENGTH - 1);
          matches[count].path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
          matches[count].symbolizer_address = addr_int - (uintptr_t)slide;
          matches[count].relative_address = false;

#ifndef NDEBUG
          log_debug("[macOS dyld] addr=%p matches %s (preferred=%lx, slide=%ld)", addr, image_name,
                    matches[count].symbolizer_address, slide);
#endif
          count++;
          break;
        }
      }
      cmd = (const struct load_command *)((uintptr_t)cmd + cmd->cmdsize);
    }
  }

  return count;
}
