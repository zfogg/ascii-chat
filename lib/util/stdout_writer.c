#include <ascii-chat/util/stdout_writer.h>
#include <errno.h>

asciichat_error_t stdout_writer_write(FILE *stream, const void *data, size_t size) {
  if (!stream || (!data && size))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing output stream or bytes");
  const unsigned char *bytes = data;
  size_t offset = 0;
  while (offset < size) {
    size_t written = fwrite(bytes + offset, 1, size - offset, stream);
    offset += written;
    if (ferror(stream)) {
      if (errno == EINTR) {
        clearerr(stream);
        continue;
      }
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot write output stream");
    }
    if (!written)
      return SET_ERRNO(ERROR_FILE_OPERATION, "Output stream made no progress");
  }
  while (fflush(stream) != 0) {
    if (errno != EINTR)
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot flush output stream");
    clearerr(stream);
  }
  return ASCIICHAT_OK;
}
