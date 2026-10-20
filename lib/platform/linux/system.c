/**
 * @file platform/linux/system.c
 * @ingroup platform
 * @brief 🐧 Linux system utilities and backtrace symbol resolution
 */

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/internal.h>
#include <ascii-chat/common.h>
#include <link.h>
#include <string.h>

typedef struct {
  uintptr_t address;
  platform_binary_match_t *match;
  bool found;
} linux_binary_lookup_t;

static int find_binary_for_address(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size;
  linux_binary_lookup_t *lookup = data;

  for (ElfW(Half) i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) *segment = &info->dlpi_phdr[i];
    if (segment->p_type != PT_LOAD || (segment->p_flags & PF_X) == 0) {
      continue;
    }

    uintptr_t loaded_start = (uintptr_t)info->dlpi_addr + (uintptr_t)segment->p_vaddr;
    uintptr_t loaded_end = loaded_start + (uintptr_t)segment->p_memsz;
    if (lookup->address < loaded_start || lookup->address >= loaded_end) {
      continue;
    }

    if (info->dlpi_name && info->dlpi_name[0]) {
      strncpy(lookup->match->path, info->dlpi_name, PLATFORM_MAX_PATH_LENGTH - 1);
      lookup->match->path[PLATFORM_MAX_PATH_LENGTH - 1] = '\0';
    } else if (!platform_get_executable_path(lookup->match->path, PLATFORM_MAX_PATH_LENGTH)) {
      return 0;
    }
    lookup->match->symbolizer_address = lookup->address - (uintptr_t)info->dlpi_addr;
    lookup->match->relative_address = false;
    lookup->found = true;
    return 1;
  }

  return 0;
}

/**
 * @brief Get binary that contains address on Linux via the ELF program headers
 *
 * Iterates loaded ELF images to find the executable segment containing the
 * runtime address. Removing the ELF load bias produces the preferred virtual
 * address expected by llvm-symbolizer.
 *
 * @param addr Runtime address from backtrace
 * @param matches Output array for matches (path, offset, is_project_lib)
 * @param max_matches Maximum number of matches to store
 * @return Number of matches found (0, 1, or rarely 2)
 */
int get_binary_file_address_offsets(const void *addr, platform_binary_match_t *matches, int max_matches) {
  if (!addr || !matches || max_matches <= 0) {
    return 0;
  }

  linux_binary_lookup_t lookup = {
      .address = (uintptr_t)addr,
      .match = &matches[0],
      .found = false,
  };
  dl_iterate_phdr(find_binary_for_address, &lookup);
  return lookup.found ? 1 : 0;
}
