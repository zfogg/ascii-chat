#include <criterion/criterion.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(positional_callbacks);

Test(positional_callbacks, client_addresses_preserve_host_port_and_transport_semantics) {
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
    cr_assert_eq(parse_client_address(cases[i].input, options, NULL, 0, &error), 1, "%s", cases[i].input);
    cr_assert_str_eq(options->address, cases[i].address);
    cr_assert_eq(options->port, cases[i].port);
    cr_assert_null(error);
  }
  cr_assert_eq(parse_client_address("blue-mountain-tiger", options, NULL, 0, NULL), 1);
  cr_assert_str_eq(options->session_string, "blue-mountain-tiger");
  const char *invalid[] = {"127.0.0.999", "2001:xyz::1", "localhost:0", "localhost:65536", "localhost:abc", "[::1]:0"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    char *error = NULL;
    cr_assert_eq(parse_client_address(invalid[i], options, NULL, 0, &error), -1);
    cr_assert_not_null(error);
    SAFE_FREE(error);
  }
  cr_assert_eq(parse_client_address(NULL, options, NULL, 0, NULL), -1);
  cr_assert_eq(parse_client_address("localhost", NULL, NULL, 0, NULL), -1);
  SAFE_FREE(options);
}

Test(positional_callbacks, server_accepts_dual_stack_and_rejects_duplicate_family) {
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  char *second[] = {"2001:db8::1"};
  cr_assert_eq(parse_server_bind_address("192.168.1.2", options, second, 1, NULL), 2);
  cr_assert_str_eq(options->address, "192.168.1.2");
  cr_assert_str_eq(options->address6, "2001:db8::1");
  char *error = NULL;
  cr_assert_eq(parse_server_bind_address("192.168.1.3", options, NULL, 0, &error), -1);
  cr_assert_not_null(error);
  SAFE_FREE(error);
  cr_assert_eq(parse_server_bind_address("2001:db8::2", options, NULL, 0, &error), -1);
  SAFE_FREE(error);
  memset(options, 0, sizeof(*options));
  char *ipv4[] = {"192.168.1.2"};
  cr_assert_eq(parse_server_bind_address("[2001:db8::1]", options, ipv4, 1, NULL), 2);
  cr_assert_str_eq(options->address, "192.168.1.2");
  cr_assert_str_eq(options->address6, "2001:db8::1");
  memset(options, 0, sizeof(*options));
  cr_assert_eq(parse_server_bind_address("192.168.1.3", options, ipv4, 1, NULL), 1);
  cr_assert_str_eq(options->address, "192.168.1.3");
  cr_assert_eq(parse_server_bind_address("example.test", options, NULL, 0, &error), -1);
  cr_assert_not_null(error);
  SAFE_FREE(error);
  cr_assert_eq(parse_server_bind_address(NULL, options, NULL, 0, NULL), -1);
  SAFE_FREE(options);
}

Test(positional_callbacks, mirror_prefers_readable_files_and_enforces_exclusive_input) {
  char path[1024];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-media", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  options_t *options = SAFE_CALLOC(1, sizeof(*options), options_t *);
  cr_assert_eq(parse_mirror_media(path, options, NULL, 0, NULL), 1);
  cr_assert_str_eq(options->media_file, path);
  cr_assert_eq(options->media_url[0], '\0');
  char *error = NULL;
  cr_assert_eq(parse_mirror_media("https://example.test/video", options, NULL, 0, &error), -1);
  cr_assert_not_null(error);
  SAFE_FREE(error);
  memset(options, 0, sizeof(*options));
  cr_assert_eq(parse_mirror_media("https://example.test/video", options, NULL, 0, NULL), 1);
  cr_assert_str_eq(options->media_url, "https://example.test/video");
  cr_assert_eq(parse_mirror_media(path, options, NULL, 0, &error), -1);
  SAFE_FREE(error);
  cr_assert_eq(parse_mirror_media(NULL, options, NULL, 0, NULL), -1);
  SAFE_FREE(options);
  cr_assert_eq(platform_delete_temp_file(path), 0);
}

Test(positional_callbacks, timestamp_formats_and_volume_boundaries) {
  const struct {
    const char *text;
    double seconds;
  } times[] = {{"0", 0}, {"30.5", 30.5}, {"2:30.5", 150.5}, {"1:02:30.5", 3750.5}};
  for (size_t i = 0; i < sizeof(times) / sizeof(*times); ++i) {
    double seconds = -1;
    cr_assert(parse_timestamp(times[i].text, &seconds, NULL));
    cr_assert_float_eq(seconds, times[i].seconds, 0.0001);
  }
  const char *invalid[] = {"", "-1", "abc", "x:30", "1:x:3", "1:60:1", "1:2:3x", "1:2:3:4"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    double seconds = 0;
    char *error = NULL;
    cr_assert_not(parse_timestamp(invalid[i], &seconds, &error));
    cr_assert_not_null(error);
    SAFE_FREE(error);
  }
  float volume = 0;
  cr_assert(parse_volume("0.25", &volume, NULL));
  cr_assert_float_eq(volume, 0.25f, 0.0001f);
  const char *invalid_volume[] = {"", "x", "-0.1", "1.1"};
  for (size_t i = 0; i < sizeof(invalid_volume) / sizeof(*invalid_volume); ++i) {
    char *error = NULL;
    cr_assert_not(parse_volume(invalid_volume[i], &volume, &error));
    cr_assert_not_null(error);
    SAFE_FREE(error);
  }
}
