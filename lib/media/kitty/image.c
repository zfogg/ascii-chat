#include <ascii-chat/media/kitty/image.h>
#include <ascii-chat/util/stdout_writer.h>
#include <limits.h>
#include <stddef.h>

asciichat_error_t kitty_image_write(FILE *stream, const uint8_t *rgba, int width, int height, int pitch) {
  if (!stream || !rgba || width <= 0 || height <= 0 || width > INT_MAX / 4 || pitch < width * 4 ||
      (size_t)height > SIZE_MAX / (size_t)pitch)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Kitty image dimensions or pixels");

  // 3072 raw bytes produce the protocol maximum of 4096 base64 bytes.
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const size_t row_bytes = (size_t)width * 4;
  const size_t total = row_bytes * (size_t)height;
  size_t offset = 0;
  bool first = true;
  while (offset < total) {
    char packet[4224];
    size_t count = total - offset;
    if (count > 3072)
      count = 3072;
    bool more = offset + count < total;
    int prefix = first ? snprintf(packet, sizeof(packet), "\033_Ga=T,f=32,s=%d,v=%d,t=d,q=2,m=%d;", width, height, more)
                       : snprintf(packet, sizeof(packet), "\033_Gm=%d;", more);
    size_t used = (size_t)prefix;
    for (size_t i = 0; i < count; i += 3) {
      uint32_t triple = 0;
      size_t available = count - i < 3 ? count - i : 3;
      for (size_t j = 0; j < available; ++j) {
        size_t index = offset + i + j;
        triple |= (uint32_t)rgba[(index / row_bytes) * (size_t)pitch + index % row_bytes] << (16 - 8 * j);
      }
      packet[used++] = alphabet[(triple >> 18) & 63];
      packet[used++] = alphabet[(triple >> 12) & 63];
      packet[used++] = available > 1 ? alphabet[(triple >> 6) & 63] : '=';
      packet[used++] = available > 2 ? alphabet[triple & 63] : '=';
    }
    packet[used++] = '\033';
    packet[used++] = '\\';
    asciichat_error_t err = stdout_writer_write(stream, packet, used);
    if (err != ASCIICHAT_OK)
      return err;
    offset += count;
    first = false;
  }
  return ASCIICHAT_OK;
}
