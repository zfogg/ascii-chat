#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/asciichat_errno.h>
#include <libplatform/system.h>
#ifdef _WIN32
namespace {
struct catch2_test_environment {
  catch2_test_environment() {
    platform_setenv("TESTING", "1");
    asciichat_shared_init(nullptr, false, true);
    errno_suppress(true);
  }
};

const catch2_test_environment environment{};
} // namespace
#endif
