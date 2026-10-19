#include <ascii-chat/platform/clipboard.h>
#include <ascii-chat/common.h>
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
    if ([files count] > 1)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Copy one media file at a time");
    NSString *text = [files count] ? [(NSURL *)files[0] path] : [board stringForType:NSPasteboardTypeString];
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
