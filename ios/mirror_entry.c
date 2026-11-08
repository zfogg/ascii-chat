#include <ascii-chat/ios/mirror.h>

/* iOS mirror mode owns the camera lifecycle and leaves presentation to SwiftUI. */
int mirror_main(void) {
  return asciichat_ios_mirror_main();
}
