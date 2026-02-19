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
 * @brief Get size of mach-O image from header
 *
 * Walks the load commands to calculate total image size.
 * Returns 0 if header is invalid.
 */
static uint64_t get_image_size_from_header(const struct mach_header_64 *header) {
  if (!header || header->magic != MH_MAGIC_64) {
    return 0;
  }

  uint64_t size = 0;
  const struct load_command *cmd = (const struct load_command *)(header + 1);

  for (uint32_t i = 0; i < header->ncmds; i++) {
    if (cmd->cmd == LC_SEGMENT_64) {
      const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
      uint64_t seg_end = seg->vmaddr + seg->vmsize;
      if (seg_end > size) {
        size = seg_end;
      }
    }
    cmd = (const struct load_command *)((uintptr_t)cmd + cmd->cmdsize);
  }

  return size;
}

/**
 * @brief Get binary that contains address on macOS via dyld
 *
 * Iterates through dyld-loaded images to find which one contains
 * the given runtime address. Returns the file offset within that image,
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

  uint32_t image_count = _dyld_image_count();
  for (uint32_t i = 0; i < image_count && count < max_matches; i++) {
    const char *image_name = _dyld_get_image_name(i);
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(i);
    intptr_t slide = _dyld_get_image_vmaddr_slide(i);

    if (!header || !image_name) {
      continue;
    }

    // Calculate image size from mach header
    uint64_t image_size = get_image_size_from_header(header);
    if (image_size == 0) {
      continue;
    }

    // Base address is header address + slide
    uintptr_t base = (uintptr_t)header + slide;
    uintptr_t end = base + image_size;

    // Check if address falls within this image
    if (addr_int >= base && addr_int < end) {
      strncpy(matches[count].path, image_name, PLATFORM_MAX_PATH_LENGTH - 1);
      matches[count].path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
      matches[count].file_offset = addr_int - base;

#ifndef NDEBUG
      log_debug("[macOS dyld] addr=%p matches %s (offset=%lx, base=%p, slide=%ld)", addr, image_name,
                matches[count].file_offset, (void *)base, slide);
#endif
      count++;
    }
  }

  return count;
}
