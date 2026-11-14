#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <libplatform/system.h>

#ifdef _WIN32
namespace {
struct catch2_test_environment {
  catch2_test_environment() {
    platform_setenv("TESTING", "1");
    log_init(nullptr, LOG_WARN, false, false);
    errno_suppress(true);
    options_state_init();
  }
};

const catch2_test_environment environment{};
} // namespace
#endif
