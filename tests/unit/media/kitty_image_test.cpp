#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/media/kitty/image.h>
#include <ascii-chat/util/stdout_writer.h>
#include <limits.h>
#include <string.h>

TEST_CASE("kitty_image/known_pixel_and_padding", "[kitty_image]") {
  FILE *stream = tmpfile();
  REQUIRE((stream) != nullptr);
  const uint8_t pixels[] = {255, 0, 0, 255, 17, 18, 19};
  REQUIRE((kitty_image_write(stream, pixels, 1, 1, sizeof(pixels))) == (ASCIICHAT_OK));
  rewind(stream);
  char actual[128] = {0};
  size_t size = fread(actual, 1, sizeof(actual), stream);
  const char expected[] = "\033_Ga=T,f=32,s=1,v=1,t=d,q=2,m=0;/wAA/w==\033\\";
  REQUIRE((size) == (strlen(expected)));
  REQUIRE(std::memcmp((actual), (expected), (size)) == 0);
  fclose(stream);
}

TEST_CASE("kitty_image/rejects_invalid_buffers_without_output", "[kitty_image]") {
  FILE *stream = tmpfile();
  REQUIRE((stream) != nullptr);
  uint8_t pixel[4] = {0};
  REQUIRE((kitty_image_write(NULL, pixel, 1, 1, 4)) != (ASCIICHAT_OK));
  REQUIRE((kitty_image_write(stream, NULL, 1, 1, 4)) != (ASCIICHAT_OK));
  REQUIRE((kitty_image_write(stream, pixel, 0, 1, 4)) != (ASCIICHAT_OK));
  REQUIRE((kitty_image_write(stream, pixel, 1, -1, 4)) != (ASCIICHAT_OK));
  REQUIRE((kitty_image_write(stream, pixel, INT_MAX, 1, 4)) != (ASCIICHAT_OK));
  REQUIRE((kitty_image_write(stream, pixel, 1, 1, 3)) != (ASCIICHAT_OK));
  REQUIRE((ftell(stream)) == (0));
  fclose(stream);
}

TEST_CASE("kitty_image/shared_writer_preserves_binary_nuls_and_stream_ownership", "[kitty_image]") {
  FILE *stream = tmpfile();
  REQUIRE((stream) != nullptr);
  const uint8_t data[] = {0, 255, '\n', '\r', 0};
  REQUIRE((stdout_writer_write(stream, data, sizeof(data))) == (ASCIICHAT_OK));
  REQUIRE((stdout_writer_write(stream, NULL, 0)) == (ASCIICHAT_OK));
  REQUIRE((stdout_writer_write(NULL, data, sizeof(data))) != (ASCIICHAT_OK));
  REQUIRE((stdout_writer_write(stream, NULL, 1)) != (ASCIICHAT_OK));
  REQUIRE((stdout_writer_write(stream, data, sizeof(data))) == (ASCIICHAT_OK));
  rewind(stream);
  uint8_t result[sizeof(data) * 2];
  REQUIRE((fread(result, 1, sizeof(result), stream)) == (sizeof(result)));
  REQUIRE(std::memcmp((result), (data), (sizeof(data))) == 0);
  REQUIRE(std::memcmp((result + sizeof(data)), (data), (sizeof(data))) == 0);
  fclose(stream);
}
