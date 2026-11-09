/**
 * @file tooling/defer/defer.h
 * @brief Marker for build-time block-scoped cleanup statements.
 *
 * defer(expression) and defer({ statements; }) run when the enclosing block
 * exits, in reverse registration order. C sources are transformed before
 * compilation; C++ sources use a scope guard and need no transformation.
 * defer_named(handle, action) registers cancellable cleanup; defer_cancel(handle)
 * cancels the action associated with that lexical name.
 */
#pragma once

#if defined(__cplusplus) && !defined(__OBJC__)
extern "C++" {
namespace ascii_defer_detail {
template <typename F> class guard {
public:
  explicit guard(F &&action) : action_(static_cast<F &&>(action)), active_(true) {}
  guard(guard &&other) : action_(static_cast<F &&>(other.action_)), active_(other.active_) {
    other.active_ = false;
  }
  guard(const guard &) = delete;
  guard &operator=(const guard &) = delete;
  ~guard() noexcept {
    if (active_)
      action_();
  }
  void cancel() noexcept {
    active_ = false;
  }

private:
  F action_;
  bool active_;
};
template <typename F> guard<F> make_guard(F action) {
  return guard<F>(static_cast<F &&>(action));
}
} // namespace ascii_defer_detail
} // extern "C++"
#define ASCII_DEFER_JOIN_IMPL(a, b) a##b
#define ASCII_DEFER_JOIN(a, b) ASCII_DEFER_JOIN_IMPL(a, b)
#define defer(...)                                                                                                     \
  auto ASCII_DEFER_JOIN(__ascii_defer_guard_, __COUNTER__) = ::ascii_defer_detail::make_guard([&]() { __VA_ARGS__; })
#define defer_named(name, ...) auto name = ::ascii_defer_detail::make_guard([&]() { __VA_ARGS__; })
#define defer_cancel(name) (name).cancel()
#elif defined(__OBJC__)
#ifdef __cplusplus
#define ASCII_DEFER_OBJC_UNSUPPORTED static_assert(false, "defer() is not supported in Objective-C yet")
#else
#define ASCII_DEFER_OBJC_UNSUPPORTED _Static_assert(0, "defer() is not supported in Objective-C yet")
#endif
#define defer(...) ASCII_DEFER_OBJC_UNSUPPORTED
#define defer_named(name, ...) ASCII_DEFER_OBJC_UNSUPPORTED
#define defer_cancel(name) ASCII_DEFER_OBJC_UNSUPPORTED
#elif defined(ASCIICHAT_DEFER_PARSING)
void __ascii_defer_marker(void);
#define defer(...)                                                                                                     \
  do {                                                                                                                 \
    __ascii_defer_marker();                                                                                            \
  } while (0)
#define defer_named(name, ...)                                                                                         \
  do {                                                                                                                 \
    __ascii_defer_marker();                                                                                            \
  } while (0)
#define defer_cancel(name)                                                                                             \
  do {                                                                                                                 \
    __ascii_defer_marker();                                                                                            \
  } while (0)
#else
#define defer(...) _Static_assert(0, "defer() requires the source transformation")
#define defer_named(name, ...) _Static_assert(0, "defer_named() requires the source transformation")
#define defer_cancel(name) _Static_assert(0, "defer_cancel() requires the source transformation")
#endif
