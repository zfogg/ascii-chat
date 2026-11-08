#import <Foundation/Foundation.h>

#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/paths.h>
#include <ascii-chat/util/string.h>
#include <string.h>

static char *ios_application_support_dir(void) {
  @autoreleasepool {
    NSArray<NSURL *> *urls = [[NSFileManager defaultManager]
        URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
    NSURL *base = urls.firstObject;
    if (!base) return NULL;
    NSURL *app = [base URLByAppendingPathComponent:@"ascii-chat" isDirectory:YES];
    const char *path = app.fileSystemRepresentation;
    if (!path) return NULL;
    size_t size = strlen(path) + 2;
    char *result = SAFE_MALLOC(size, char *);
    if (result) safe_snprintf(result, size, "%s/", path);
    return result;
  }
}

asciichat_error_t platform_get_data_dir_candidates(platform_directory_list_t *list_out) {
  if (!list_out) return SET_ERRNO(ERROR_INVALID_PARAM, "list_out cannot be NULL");
  list_out->candidates = SAFE_CALLOC(1, sizeof(*list_out->candidates), platform_directory_candidate_t *);
  if (!list_out->candidates) return SET_ERRNO(ERROR_MEMORY, "Failed to allocate iOS data directory");
  list_out->candidates[0].path = ios_application_support_dir();
  list_out->candidates[0].mode = DIR_PERM_PRIVATE;
  list_out->count = list_out->candidates[0].path ? 1 : 0;
  return list_out->count ? ASCIICHAT_OK : SET_ERRNO(ERROR_NOT_FOUND, "iOS Application Support directory unavailable");
}
