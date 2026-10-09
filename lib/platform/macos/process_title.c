/*
 * Launch Services process naming adapted from libuv's darwin-proctitle.c.
 * Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "../process_title_internal.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

asciichat_error_t platform_process_title_native_set(const char *title, const char *short_name) {
  if (!pthread_main_np()) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Process title must be set by the main thread");
  }
  char thread_name[64];
  size_t length = strlen(short_name);
  if (length >= sizeof(thread_name)) {
    length = sizeof(thread_name) - 1;
  }
  memcpy(thread_name, short_name, length);
  thread_name[length] = '\0';
  int thread_error = pthread_setname_np(thread_name);

  // These Launch Services functions are private. Resolve every symbol before
  // using it so an unavailable framework/session leaves the ps title usable.
  void *services =
      dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices", RTLD_LAZY | RTLD_LOCAL);
  if (!services) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Activity Monitor naming: ApplicationServices unavailable");
  }
  CFBundleRef bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.apple.LaunchServices"));
  CFTypeRef (*get_asn)(void) = NULL;
  OSStatus (*set_information)(int, CFTypeRef, CFStringRef, CFStringRef, CFDictionaryRef *) = NULL;
  CFDictionaryRef (*check_in)(int, CFDictionaryRef) = NULL;
  void (*set_connection)(uint64_t, void *) = NULL;
  CFStringRef *display_name = NULL;
  if (bundle) {
    *(void **)(&get_asn) = CFBundleGetFunctionPointerForName(bundle, CFSTR("_LSGetCurrentApplicationASN"));
    *(void **)(&set_information) = CFBundleGetFunctionPointerForName(bundle, CFSTR("_LSSetApplicationInformationItem"));
    *(void **)(&check_in) = CFBundleGetFunctionPointerForName(bundle, CFSTR("_LSApplicationCheckIn"));
    *(void **)(&set_connection) =
        CFBundleGetFunctionPointerForName(bundle, CFSTR("_LSSetApplicationLaunchServicesServerConnectionStatus"));
    display_name = CFBundleGetDataPointerForName(bundle, CFSTR("_kLSDisplayNameKey"));
  }
  if (!get_asn || !set_information || !check_in || !set_connection || !display_name || !*display_name) {
    dlclose(services);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Activity Monitor naming: Launch Services API unavailable");
  }
  static bool checked_in;
  if (!checked_in) {
    CFBundleRef main_bundle = CFBundleGetMainBundle();
    CFDictionaryRef info = main_bundle ? CFBundleGetInfoDictionary(main_bundle) : NULL;
    CFMutableDictionaryRef properties =
        info ? CFDictionaryCreateMutableCopy(NULL, 0, info)
             : CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!properties) {
      dlclose(services);
      return SET_ERRNO(ERROR_MEMORY, "Activity Monitor naming: could not allocate application properties");
    }
    // A terminal program must not acquire a Dock icon or activate a GUI window.
    CFDictionarySetValue(properties, CFSTR("LSUIElement"), kCFBooleanTrue);
    set_connection(0, NULL);
    check_in(-2, properties);
    CFRelease(properties);
    checked_in = get_asn() != NULL;
  }
  CFTypeRef asn = get_asn();
  CFStringRef name = CFStringCreateWithCString(NULL, title, kCFStringEncodingUTF8);
  OSStatus status = -1;
  if (asn && name) {
    status = set_information(-2, asn, *display_name, name, NULL);
  }
  if (name) {
    CFRelease(name);
  }
  dlclose(services);
  if (status != 0) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Activity Monitor naming failed (status %d)", (int)status);
  }
  if (thread_error != 0) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "macOS main-thread naming failed (error %d)", thread_error);
  }
  return ASCIICHAT_OK;
}
