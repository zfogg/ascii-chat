#import <UIKit/UIKit.h>
#import <dispatch/dispatch.h>

#include <ascii-chat/platform/abstraction.h>

static void set_idle_timer_disabled(BOOL disabled) {
    void (^update)(void) = ^{
        UIApplication.sharedApplication.idleTimerDisabled = disabled;
    };

    if ([NSThread isMainThread]) {
        update();
    } else {
        dispatch_sync(dispatch_get_main_queue(), update);
    }
}

asciichat_error_t platform_enable_keepawake(void) {
    set_idle_timer_disabled(YES);
    return ASCIICHAT_OK;
}

void platform_disable_keepawake(void) {
    set_idle_timer_disabled(NO);
}
