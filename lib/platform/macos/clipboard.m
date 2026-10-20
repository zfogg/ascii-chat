#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/common.h>
#include <ascii-chat/media/playlist.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>

asciichat_error_t platform_clipboard_read(char *output, size_t capacity) {
  if (!output || capacity < 2)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid clipboard buffer");
  output[0] = '\0';
  @autoreleasepool {
    NSPasteboard *board = [NSPasteboard generalPasteboard];
    NSArray *files = [board readObjectsForClasses:@[ [NSURL class] ]
                                          options:@{
                                            NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                          }];
    if ([files count]) {
      media_playlist_t list = {0};
      for (NSURL *file in files) {
        asciichat_error_t result = media_playlist_append(&list, [[file path] UTF8String], false);
        if (result != ASCIICHAT_OK)
          return result;
      }
      return media_playlist_format(&list, output, capacity);
    }
    NSString *text = [board stringForType:NSPasteboardTypeString];
    const char *utf8 = [text UTF8String];
    if (!utf8 || !*utf8)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Clipboard contains no text or copied file");
    if (strlen(utf8) >= capacity)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Clipboard text is too long");
    memcpy(output, utf8, strlen(utf8) + 1);
  }
  return ASCIICHAT_OK;
}

#else
asciichat_error_t platform_clipboard_read(char *output, size_t capacity) {
  if (output && capacity)
    output[0] = '\0';
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Native CLI clipboard input is unavailable on this platform");
}
#endif
