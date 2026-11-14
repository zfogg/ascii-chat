#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/media/kitty/image.h>
#include <ascii-chat/util/stdout_writer.h>
#include <limits.h>
#include <string.h>

TEST_CASE("kitty_image/known_pixel_and_padding", "[kitty_image]") {
  FILE *stream = tmpfile();
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  const uint8_t pixels[] = {255, 0, 0, 255, 17, 18, 19};
  ASCIICHAT_REQUIRE_EQ(kitty_image_write(stream, pixels, 1, 1, sizeof(pixels)), ASCIICHAT_OK);
  rewind(stream);
  char actual[128] = {0};
  size_t size = fread(actual, 1, sizeof(actual), stream);
  const char expected[] = "\033_Ga=T,f=32,s=1,v=1,t=d,q=2,m=0;/wAA/w==\033\\";
  ASCIICHAT_REQUIRE_EQ(size, strlen(expected));
  ASCIICHAT_REQUIRE_ARRAY_EQ(actual, expected, size);
  fclose(stream);
}

TEST_CASE("kitty_image/rejects_invalid_buffers_without_output", "[kitty_image]") {
  FILE *stream = tmpfile();
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  uint8_t pixel[4] = {0};
  ASCIICHAT_REQUIRE_NE(kitty_image_write(NULL, pixel, 1, 1, 4), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(kitty_image_write(stream, NULL, 1, 1, 4), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(kitty_image_write(stream, pixel, 0, 1, 4), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(kitty_image_write(stream, pixel, 1, -1, 4), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(kitty_image_write(stream, pixel, INT_MAX, 1, 4), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(kitty_image_write(stream, pixel, 1, 1, 3), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ftell(stream), 0);
  fclose(stream);
}

TEST_CASE("kitty_image/shared_writer_preserves_binary_nuls_and_stream_ownership", "[kitty_image]") {
  FILE *stream = tmpfile();
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  const uint8_t data[] = {0, 255, '\n', '\r', 0};
  ASCIICHAT_REQUIRE_EQ(stdout_writer_write(stream, data, sizeof(data)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(stdout_writer_write(stream, NULL, 0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(stdout_writer_write(NULL, data, sizeof(data)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(stdout_writer_write(stream, NULL, 1), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(stdout_writer_write(stream, data, sizeof(data)), ASCIICHAT_OK);
  rewind(stream);
  uint8_t result[sizeof(data) * 2];
  ASCIICHAT_REQUIRE_EQ(fread(result, 1, sizeof(result), stream), sizeof(result));
  ASCIICHAT_REQUIRE_ARRAY_EQ(result, data, sizeof(data));
  ASCIICHAT_REQUIRE_ARRAY_EQ(result + sizeof(data), data, sizeof(data));
  fclose(stream);
}
