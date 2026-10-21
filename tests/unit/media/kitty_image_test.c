#include <criterion/criterion.h>
#include <ascii-chat/media/kitty/image.h>
#include <ascii-chat/util/stdout_writer.h>
#include <limits.h>
#include <string.h>

Test(kitty_image, known_pixel_and_padding) {
  FILE *stream = tmpfile();
  cr_assert_not_null(stream);
  const uint8_t pixels[] = {255, 0, 0, 255, 17, 18, 19};
  cr_assert_eq(kitty_image_write(stream, pixels, 1, 1, sizeof(pixels)), ASCIICHAT_OK);
  rewind(stream);
  char actual[128] = {0};
  size_t size = fread(actual, 1, sizeof(actual), stream);
  const char expected[] = "\033_Ga=T,f=32,s=1,v=1,t=d,q=2,m=0;/wAA/w==\033\\";
  cr_assert_eq(size, strlen(expected));
  cr_assert_arr_eq(actual, expected, size);
  fclose(stream);
}

Test(kitty_image, rejects_invalid_buffers_without_output) {
  FILE *stream = tmpfile();
  cr_assert_not_null(stream);
  uint8_t pixel[4] = {0};
  cr_assert_neq(kitty_image_write(NULL, pixel, 1, 1, 4), ASCIICHAT_OK);
  cr_assert_neq(kitty_image_write(stream, NULL, 1, 1, 4), ASCIICHAT_OK);
  cr_assert_neq(kitty_image_write(stream, pixel, 0, 1, 4), ASCIICHAT_OK);
  cr_assert_neq(kitty_image_write(stream, pixel, 1, -1, 4), ASCIICHAT_OK);
  cr_assert_neq(kitty_image_write(stream, pixel, INT_MAX, 1, 4), ASCIICHAT_OK);
  cr_assert_neq(kitty_image_write(stream, pixel, 1, 1, 3), ASCIICHAT_OK);
  cr_assert_eq(ftell(stream), 0);
  fclose(stream);
}

Test(kitty_image, shared_writer_preserves_binary_nuls_and_stream_ownership) {
  FILE *stream = tmpfile();
  cr_assert_not_null(stream);
  const uint8_t data[] = {0, 255, '\n', '\r', 0};
  cr_assert_eq(stdout_writer_write(stream, data, sizeof(data)), ASCIICHAT_OK);
  cr_assert_eq(stdout_writer_write(stream, NULL, 0), ASCIICHAT_OK);
  cr_assert_neq(stdout_writer_write(NULL, data, sizeof(data)), ASCIICHAT_OK);
  cr_assert_neq(stdout_writer_write(stream, NULL, 1), ASCIICHAT_OK);
  cr_assert_eq(stdout_writer_write(stream, data, sizeof(data)), ASCIICHAT_OK);
  rewind(stream);
  uint8_t result[sizeof(data) * 2];
  cr_assert_eq(fread(result, 1, sizeof(result), stream), sizeof(result));
  cr_assert_arr_eq(result, data, sizeof(data));
  cr_assert_arr_eq(result + sizeof(data), data, sizeof(data));
  fclose(stream);
}
