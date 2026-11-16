#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/options/parsers.h>
#include <options/options.h>
#include <libplatform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("positional_callbacks/client_addresses_preserve_host_port_and_transport_semantics", "[positional_callbacks]") {
  const struct {
    const char *input, *address;
    int port;
  } cases[] = {{"127.0.0.1:1234", "127.0.0.1", 1234},
               {"example.test:65535", "example.test", 65535},
               {"[2001:db8::1]:42", "2001:db8::1", 42},
               {"::1", "::1", 27224},
               {"localhost", "localhost", 27224},
               {"wss://example.test/chat", "wss://example.test/chat", 27224},
               {"ws://example.test", "ws://example.test", 27224},
               {"tcp://example.test:12", "tcp://example.test:12", 27224}};
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
    memset(options, 0, sizeof(*options));
    options->port = 27224;
    char *error = NULL;
    REQUIRE((parse_client_address(cases[i].input, options, NULL, 0, &error)) == (1));
    REQUIRE(std::strcmp((options->address), (cases[i].address)) == 0);
    REQUIRE((options->port) == (cases[i].port));
    REQUIRE((error) == nullptr);
  }
  REQUIRE((parse_client_address("blue-mountain-tiger", options, NULL, 0, NULL)) == (1));
  REQUIRE(std::strcmp((options->session_string), ("blue-mountain-tiger")) == 0);
  const char *invalid[] = {"127.0.0.999", "2001:xyz::1", "localhost:0", "localhost:65536", "localhost:abc", "[::1]:0"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    char *error = NULL;
    REQUIRE((parse_client_address(invalid[i], options, NULL, 0, &error)) == (-1));
    REQUIRE((error) != nullptr);
    SAFE_FREE(error);
  }
  REQUIRE((parse_client_address(NULL, options, NULL, 0, NULL)) == (-1));
  REQUIRE((parse_client_address("localhost", NULL, NULL, 0, NULL)) == (-1));
  SAFE_FREE(options);
}

TEST_CASE("positional_callbacks/server_accepts_dual_stack_and_rejects_duplicate_family", "[positional_callbacks]") {
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  char *second[] = {"2001:db8::1"};
  REQUIRE((parse_server_bind_address("192.168.1.2", options, second, 1, NULL)) == (2));
  REQUIRE(std::strcmp((options->address), ("192.168.1.2")) == 0);
  REQUIRE(std::strcmp((options->address6), ("2001:db8::1")) == 0);
  char *error = NULL;
  REQUIRE((parse_server_bind_address("192.168.1.3", options, NULL, 0, &error)) == (-1));
  REQUIRE((error) != nullptr);
  SAFE_FREE(error);
  REQUIRE((parse_server_bind_address("2001:db8::2", options, NULL, 0, &error)) == (-1));
  SAFE_FREE(error);
  memset(options, 0, sizeof(*options));
  char *ipv4[] = {"192.168.1.2"};
  REQUIRE((parse_server_bind_address("[2001:db8::1]", options, ipv4, 1, NULL)) == (2));
  REQUIRE(std::strcmp((options->address), ("192.168.1.2")) == 0);
  REQUIRE(std::strcmp((options->address6), ("2001:db8::1")) == 0);
  memset(options, 0, sizeof(*options));
  REQUIRE((parse_server_bind_address("192.168.1.3", options, ipv4, 1, NULL)) == (1));
  REQUIRE(std::strcmp((options->address), ("192.168.1.3")) == 0);
  REQUIRE((parse_server_bind_address("example.test", options, NULL, 0, &error)) == (-1));
  REQUIRE((error) != nullptr);
  SAFE_FREE(error);
  REQUIRE((parse_server_bind_address(NULL, options, NULL, 0, NULL)) == (-1));
  SAFE_FREE(options);
}

TEST_CASE("positional_callbacks/mirror_prefers_readable_files_and_enforces_exclusive_input", "[positional_callbacks]") {
  char path[1024];
  int fd;
  REQUIRE((platform_create_temp_file(path, sizeof(path), "ascii-media", &fd)) == (0));
  if (fd >= 0)
    platform_close(fd);
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  REQUIRE((parse_mirror_media(path, options, NULL, 0, NULL)) == (1));
  REQUIRE(std::strcmp((options->media_file), (path)) == 0);
  REQUIRE((options->media_url[0]) == ('\0'));
  char *error = NULL;
  REQUIRE((parse_mirror_media("https://example.test/video", options, NULL, 0, &error)) == (-1));
  REQUIRE((error) != nullptr);
  SAFE_FREE(error);
  memset(options, 0, sizeof(*options));
  REQUIRE((parse_mirror_media("https://example.test/video", options, NULL, 0, NULL)) == (1));
  REQUIRE(std::strcmp((options->media_url), ("https://example.test/video")) == 0);
  REQUIRE((parse_mirror_media(path, options, NULL, 0, &error)) == (-1));
  SAFE_FREE(error);
  REQUIRE((parse_mirror_media(NULL, options, NULL, 0, NULL)) == (-1));
  SAFE_FREE(options);
  REQUIRE((platform_delete_temp_file(path)) == (0));
}

TEST_CASE("positional_callbacks/timestamp_formats_and_volume_boundaries", "[positional_callbacks]") {
  const struct {
    const char *text;
    double seconds;
  } times[] = {{"0", 0}, {"30.5", 30.5}, {"2:30.5", 150.5}, {"1:02:30.5", 3750.5}};
  for (size_t i = 0; i < sizeof(times) / sizeof(*times); ++i) {
    double seconds = -1;
    REQUIRE(parse_timestamp(times[i].text, &seconds, NULL));
    REQUIRE((seconds) == Catch::Approx(times[i].seconds));
  }
  const char *invalid[] = {"", "-1", "abc", "x:30", "1:x:3", "1:60:1", "1:2:3x", "1:2:3:4"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    double seconds = 0;
    char *error = NULL;
    REQUIRE(!(parse_timestamp(invalid[i], &seconds, &error)));
    REQUIRE((error) != nullptr);
    SAFE_FREE(error);
  }
  float volume = 0;
  REQUIRE(parse_volume("0.25", &volume, NULL));
  REQUIRE((volume) == Catch::Approx(0.25f));
  const char *invalid_volume[] = {"", "x", "-0.1", "1.1"};
  for (size_t i = 0; i < sizeof(invalid_volume) / sizeof(*invalid_volume); ++i) {
    char *error = NULL;
    REQUIRE(!(parse_volume(invalid_volume[i], &volume, &error)));
    REQUIRE((error) != nullptr);
    SAFE_FREE(error);
  }
}
