#pragma once

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators_all.hpp>
#include <cstring>

#include <libplatform/atomic.h>
#include <libplatform/cond.h>
#include <libplatform/filesystem.h>
#include <libplatform/mutex.h>
#include <libplatform/socket.h>
#include <libplatform/thread.h>
#include <libplatform/util/string.h>

extern "C" {
#include <ascii-chat/crypto/gpg/agent.h>
#include <ascii-chat/crypto/gpg/gpg.h>
#include <ascii-chat/crypto/gpg/openpgp.h>
#include <ascii-chat/crypto/gpg/signing.h>
#include <ascii-chat/crypto/gpg/verification.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/https_keys.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/crypto/known_hosts.h>
#include <ascii-chat/crypto/pem.h>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/crypto/ssh/ssh_keys.h>
#include <ascii-chat/crypto/record.h>
#include <ascii-chat/crypto/rekey.h>
#include <ascii-chat/crypto/key_identity/keymask.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/discovery/database.h>
#include <ascii-chat/discovery/session.h>
#include <ascii-chat/discovery/key_set.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <ascii-chat/network/acip/acds_handlers.h>
#include <ascii-chat/log/grep.h>
#include <ascii-chat/log/json.h>
#include <ascii-chat/log/format.h>
#include <ascii-chat/log/colorize.h>
#include <ascii-chat/log/search.h>
#include <ascii-chat/session/session_log_buffer.h>
#include <ascii-chat/tui/notice.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/util/stdout_writer.h>
#include <ascii-chat/font.h>
#include <ascii-chat/media/kitty/image.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/consensus/coordinator.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/packet/queue.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/terminal/fd/reader.h>
#include <ascii-chat/terminal/fd/writer.h>
#include <ascii-chat/tooling/query/query.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/ui/popup.h>
#include <ascii-chat/ui/help_menu.h>
#include <ascii-chat/ui/viewport.h>
#include <ascii-chat/ui/scrollbar.h>
#include <ascii-chat/ui/text.h>
#include <ascii-chat/ui/table.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/buttons.h>
#include <ascii-chat/ui/layout.h>
#include <ascii-chat/ui/progress.h>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/tui/invitation.h>
#include <ascii-chat/tui/terminal_screen.h>
#include <ascii-chat/tui/too_small.h>
#include <ascii-chat/ui/selectable_list.h>
#include <ascii-chat/util/aspect_ratio.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/video/anim/test_pattern.h>
#include <ascii-chat/video/anim/digital_rain.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/simd/cells.h>
#include <libplatform/cpu.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/network/consensus/election.h>
#include <ascii-chat/network/consensus/handoff.h>
#include <ascii-chat/network/consensus/metrics.h>
#include <ascii-chat/network/consensus/state.h>
#include <ascii-chat/network/consensus/topology.h>
#include <ascii-chat/network/frame_validator.h>
#include <ascii-chat/network/connection_endpoint.h>
#include <ascii-chat/network/http/server/basic_auth.h>
#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/network/http/client/client.h>
#include <ascii-chat/network/update_checker.h>
#include <ascii-chat/network/webrtc/turn_credentials.h>
#include <ascii-chat/network/webrtc/turn.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/config.h>
#include "http_renderer.h"
extern uint64_t g_snapshot_last_capture_elapsed_ns;
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/options/strings.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/options/actions.h>
#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/manpage/parser.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/audio/analysis.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/tui/session.h>
#include <common/session/capture.h>
#include <common/session/display.h>
#include <common/session/host.h>
#include <common/session/participant.h>
#include <common/session/frame_validation.h>
#include <common/session/server_like.h>
#include <ascii-chat/video/rgba/video_frame.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <libplatform/terminal.h>
#include <libplatform/system.h>
#include <gotel/testing.h>
}



// Temporary mechanical migration vocabulary. Each use is removed as the
// corresponding suite is rewritten to idiomatic Catch2 sections/generators.
#define ASCIICHAT_CATCH2_TEST_NAME(suite, name) #suite "/" #name
#define ASCIICHAT_CATCH2_TEST_TAG(suite) "[" #suite "]"
#define ASCIICHAT_REQUIRE(condition, ...) REQUIRE(static_cast<bool>(condition))
#define ASCIICHAT_CHECK(condition, ...) CHECK(static_cast<bool>(condition))
#define ASCIICHAT_REQUIRE_EQ(lhs, rhs, ...) REQUIRE((lhs) == (rhs))
#define ASCIICHAT_CHECK_EQ(lhs, rhs, ...) CHECK((lhs) == (rhs))
#define ASCIICHAT_REQUIRE_NE(lhs, rhs, ...) REQUIRE((lhs) != (rhs))
#define ASCIICHAT_CHECK_NE(lhs, rhs, ...) CHECK((lhs) != (rhs))
#define ASCIICHAT_CHECK_GT(lhs, rhs, ...) CHECK((lhs) > (rhs))
#define ASCIICHAT_CHECK_GE(lhs, rhs, ...) CHECK((lhs) >= (rhs))
#define ASCIICHAT_CHECK_LT(lhs, rhs, ...) CHECK((lhs) < (rhs))
#define ASCIICHAT_CHECK_LE(lhs, rhs, ...) CHECK((lhs) <= (rhs))
#define ASCIICHAT_REQUIRE_GT(lhs, rhs, ...) REQUIRE((lhs) > (rhs))
#define ASCIICHAT_REQUIRE_GE(lhs, rhs, ...) REQUIRE((lhs) >= (rhs))
#define ASCIICHAT_REQUIRE_LT(lhs, rhs, ...) REQUIRE((lhs) < (rhs))
#define ASCIICHAT_REQUIRE_LE(lhs, rhs, ...) REQUIRE((lhs) <= (rhs))
#define ASCIICHAT_REQUIRE_NOT(condition, ...) REQUIRE(!(condition))
#define ASCIICHAT_REQUIRE_NOT_NULL(value, ...) REQUIRE((value) != nullptr)
#define ASCIICHAT_REQUIRE_NULL(value, ...) REQUIRE((value) == nullptr)
#define ASCIICHAT_CHECK_NOT_NULL(value, ...) CHECK((value) != nullptr)
#define ASCIICHAT_CHECK_NULL(value, ...) CHECK((value) == nullptr)
#define ASCIICHAT_CHECK_NOT(condition, ...) CHECK(!(condition))
#define ASCIICHAT_REQUIRE_STR_EQ(lhs, rhs, ...) REQUIRE(std::strcmp((lhs), (rhs)) == 0)
#define ASCIICHAT_CHECK_STR_EQ(lhs, rhs, ...) CHECK(std::strcmp((lhs), (rhs)) == 0)
#define ASCIICHAT_REQUIRE_STR_NE(lhs, rhs, ...) REQUIRE(std::strcmp((lhs), (rhs)) != 0)
#define ASCIICHAT_CHECK_STR_NE(lhs, rhs, ...) CHECK(std::strcmp((lhs), (rhs)) != 0)
#define ASCIICHAT_REQUIRE_FLOAT_EQ(lhs, rhs, ...) REQUIRE((lhs) == Catch::Approx(rhs))
#define ASCIICHAT_REQUIRE_FLOAT_NE(lhs, rhs, ...) REQUIRE((lhs) != Catch::Approx(rhs))
#define ASCIICHAT_REQUIRE_ARRAY_EQ(lhs, rhs, count, ...) REQUIRE(std::memcmp((lhs), (rhs), (count)) == 0)
#define ASCIICHAT_REQUIRE_STR_EMPTY(value, ...) REQUIRE((value)[0] == '\0')
#define ASCIICHAT_REQUIRE_STR_NOT_EMPTY(value, ...) REQUIRE((value)[0] != '\0')
#define ASCIICHAT_SKIP(...) SKIP("test skipped")
#define ASCIICHAT_ASSUME(condition) \
    do {                  \
        if (!(condition)) SKIP("assumption not satisfied"); \
    } while (false)
#define ASCIICHAT_FAIL(...) FAIL(__VA_ARGS__)

// Logging calls are diagnostic output only. Catch2's INFO keeps
// the message attached to the current assertion without changing test state.
#define ASCIICHAT_INFO(...) INFO(__VA_ARGS__)
#define ASCIICHAT_WARN(...) WARN(__VA_ARGS__)
#define ASCIICHAT_ERROR(...) INFO(__VA_ARGS__)
