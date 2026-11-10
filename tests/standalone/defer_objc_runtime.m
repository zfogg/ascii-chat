#include <ascii-chat/tooling/defer/defer.h>

#ifdef ASCIICHAT_TEST_GNUSTEP_OBJC
#include <objc/runtime.h>
__attribute__((objc_root_class))
@interface DeferredException
@end
@implementation DeferredException
@end
static id make_exception(void) {
  return class_createInstance(objc_getClass("DeferredException"), 0);
}
#define DEFER_TEST_POOL if (1)
#else
#import <Foundation/Foundation.h>
static id make_exception(void) {
  return [NSException exceptionWithName:@"defer test" reason:nil userInfo:nil];
}
#define DEFER_TEST_POOL @autoreleasepool
#endif

static int sequence;
static void (^global_block)(void) = ^{
  defer(sequence = sequence * 10 + 7);
};

static int run_function(int mode) {
  int value = 3;
  defer(sequence = sequence * 10 + value);
  defer_named(second, sequence = sequence * 10 + 8);
  if (mode == 1)
    defer_cancel(second);
  if (mode == 2)
    @throw make_exception();
  if (mode == 3)
    return value++;
  return 4;
}

#ifdef ASCIICHAT_TEST_GNUSTEP_OBJC
__attribute__((objc_root_class))
@interface DeferredProbe
#else
@interface DeferredProbe : NSObject
#endif
+ (int)run;
@end

@implementation DeferredProbe
+ (int)run {
  defer(sequence = sequence * 10 + 5);
  return 9;
}
@end

static void run_block(void) {
  defer(sequence = sequence * 10 + 1);
  void (^block)(void) = ^{
    defer(sequence = sequence * 10 + 6);
    return;
  };
  block();
  block();
}

static void run_block_throw(void) {
  defer(sequence = sequence * 10 + 1);
  void (^block)(void) = ^{
    defer(sequence = sequence * 10 + 2);
    @throw make_exception();
  };
  block();
}

static void run_cleanup_throw(void) {
  defer(sequence = sequence * 10 + 1);
  defer({
    sequence = sequence * 10 + 2;
    @throw make_exception();
  });
}

static void run_caught_inside_scope(void) {
  defer(sequence = sequence * 10 + 1);
  @try {
    @throw make_exception();
  } @catch (id exception) {
    (void)exception;
    sequence = 2;
  }
  sequence = sequence * 10 + 3;
}

static void run_cancel_then_throw(void) {
  defer(sequence = sequence * 10 + 1);
  defer_named(canceled, sequence = sequence * 10 + 2);
  defer_cancel(canceled);
  @throw make_exception();
}

#define CLEAN_AT_EXIT(x) defer(sequence = sequence * 10 + (x))
static void run_wrapped(void) {
  int value = 3;
  CLEAN_AT_EXIT(value);
  value = 4;
}

static void run_block_capture(void) {
  __block int value = 3;
  void (^block)(void) = ^{
    defer(sequence = sequence * 10 + value);
    value = 4;
  };
  block();
}

static void run_loop(void) {
  for (int i = 0; i < 3; ++i) {
    defer(sequence = sequence * 10 + i + 1);
    if (i == 0)
      continue;
    break;
  }
}

static void run_goto(void) {
  defer(sequence = sequence * 10 + 1);
  {
    defer(sequence = sequence * 10 + 2);
    goto after;
  }
after:
  sequence = sequence * 10 + 3;
}

#ifndef ASCIICHAT_TEST_GNUSTEP_OBJC
static void run_fast_enumeration(void) {
  for (NSNumber *number in @[ @1, @2, @3 ]) {
    defer(sequence = sequence * 10 + number.intValue);
    if (number.intValue == 1)
      continue;
    break;
  }
}
#endif

int main(void) {
  DEFER_TEST_POOL {
    sequence = 0;
    if (run_function(1) != 4 || sequence != 3)
      return 1;
    sequence = 0;
    @try {
      run_function(2);
      return 2;
    } @catch (id exception) {
      (void)exception;
    }
    if (sequence != 83)
      return 3;
    sequence = 0;
    if (run_function(3) != 3 || sequence != 84)
      return 4;
    sequence = 0;
    if ([DeferredProbe run] != 9 || sequence != 5)
      return 5;
    sequence = 0;
    run_block();
    if (sequence != 661)
      return 6;
    sequence = 0;
    global_block();
    if (sequence != 7)
      return 12;
    sequence = 0;
    run_loop();
    if (sequence != 12)
      return 7;
    sequence = 0;
    run_goto();
    if (sequence != 231)
      return 8;
    sequence = 0;
    @try {
      run_block_throw();
      return 9;
    } @catch (id exception) {
      (void)exception;
    }
    if (sequence != 21)
      return 10;
    sequence = 0;
    @try {
      run_cleanup_throw();
      return 13;
    } @catch (id exception) {
      (void)exception;
    }
    if (sequence != 21)
      return 14;
    sequence = 0;
    run_caught_inside_scope();
    if (sequence != 231)
      return 15;
    sequence = 0;
    @try {
      run_cancel_then_throw();
      return 16;
    } @catch (id exception) {
      (void)exception;
    }
    if (sequence != 1)
      return 17;
    sequence = 0;
    run_wrapped();
    if (sequence != 4)
      return 18;
    sequence = 0;
    run_block_capture();
    if (sequence != 4)
      return 19;
#ifndef ASCIICHAT_TEST_GNUSTEP_OBJC
    sequence = 0;
    run_fast_enumeration();
    if (sequence != 12)
      return 11;
#endif
    return 0;
  }
}
