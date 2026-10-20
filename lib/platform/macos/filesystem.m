/**
 * @file platform/macos/filesystem.m
 * @brief macOS filesystem operations
 * @ingroup platform
 */

#ifdef __APPLE__

#import <Foundation/Foundation.h>

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/paths.h>
#include <ascii-chat/util/string.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <string.h>

static char *copy_application_support_dir(NSSearchPathDomainMask domain) {
  @autoreleasepool {
    NSArray<NSURL *> *urls = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                                    inDomains:domain];
    NSURL *base_url = urls.firstObject;
    if (!base_url) {
      return NULL;
    }

    NSURL *app_url = [base_url URLByAppendingPathComponent:@"ascii-chat" isDirectory:YES];
    const char *path = app_url.fileSystemRepresentation;
    if (!path) {
      return NULL;
    }

    size_t len = strlen(path) + strlen("/") + 1;
    char *result = SAFE_MALLOC(len, char *);
    if (!result) {
      return NULL;
    }
    safe_snprintf(result, len, "%s/", path);
    return result;
  }
}

static void append_candidate(platform_directory_list_t *list, char *path, int mode) {
  if (!path) {
    return;
  }

  list->candidates[list->count].path = path;
  list->candidates[list->count].mode = mode;
  list->count++;
}

asciichat_error_t platform_get_data_dir_candidates(platform_directory_list_t *list_out) {
  if (!list_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "list_out cannot be NULL");
  }

  const bool include_usr_local = strcmp(ASCIICHAT_INSTALL_PREFIX, "/usr/local") != 0;
  const size_t capacity = include_usr_local ? 4 : 3;
  list_out->candidates = SAFE_CALLOC(capacity, sizeof(*list_out->candidates), platform_directory_candidate_t *);
  list_out->count = 0;
  if (!list_out->candidates) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate application data directory list");
  }

  append_candidate(list_out, copy_application_support_dir(NSUserDomainMask), DIR_PERM_PRIVATE);
  append_candidate(list_out, copy_application_support_dir(NSLocalDomainMask), 0755);

  size_t prefix_len = strlen(ASCIICHAT_INSTALL_PREFIX) + strlen("/var/ascii-chat/") + 1;
  char *prefix_dir = SAFE_MALLOC(prefix_len, char *);
  if (!prefix_dir) {
    platform_directory_list_destroy(list_out);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate application data directory path");
  }
  safe_snprintf(prefix_dir, prefix_len, "%s/var/ascii-chat/", ASCIICHAT_INSTALL_PREFIX);
  append_candidate(list_out, prefix_dir, 0755);

  if (include_usr_local) {
    char *usr_local_dir = platform_strdup("/usr/local/var/ascii-chat/");
    if (!usr_local_dir) {
      platform_directory_list_destroy(list_out);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate application data directory path");
    }
    append_candidate(list_out, usr_local_dir, 0755);
  }

  return ASCIICHAT_OK;
}

/* platform_get_executable_path is implemented in lib/platform/filesystem.c with platform-specific logic */

#endif
