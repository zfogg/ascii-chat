#include <criterion/criterion.h>
#include <ascii-chat/terminal/fd/reader.h>
#include <ascii-chat/terminal/fd/writer.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/common.h>
#include <fcntl.h>
#include <string.h>

static char path[512];
static int fd = -1;
static terminal_fd_reader_t *reader;
static terminal_fd_writer_t *writer;
static char *frame;

static void setup(void) {
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "terminal-stream", &fd), 0);
  cr_assert_eq(platform_temp_file_open("terminal-stream", path, &fd), ASCIICHAT_OK);
}

static void cleanup(void) {
  SAFE_FREE(frame);
  terminal_fd_reader_destroy(reader);
  terminal_fd_writer_destroy(writer);
  reader = NULL;
  writer = NULL;
  if (fd >= 0)
    platform_close(fd);
  fd = -1;
  if (path[0])
    platform_delete_temp_file(path);
  path[0] = '\0';
}

static void open_reader(int height) {
  if (fd >= 0)
    platform_close(fd);
  fd = platform_open("terminal-read-fixture", path, O_RDONLY);
  cr_assert_geq(fd, 0);
  cr_assert_eq(terminal_fd_reader_create(fd, height, &reader), ASCIICHAT_OK);
  fd = -1; // Reader owns the descriptor.
}

TestSuite(terminal_fd_stream, .init = setup, .fini = cleanup);

Test(terminal_fd_stream, writer_and_reader_preserve_ansi_frames_and_partial_final_frame) {
  const char first[] = "\033[31mred\033[0m\nblue";
  cr_assert_eq(terminal_fd_writer_create(fd, &writer), ASCIICHAT_OK);
  fd = -1;
  cr_assert_eq(terminal_fd_writer_write(writer, first), ASCIICHAT_OK);
  cr_assert_eq(terminal_fd_writer_write(writer, NULL), ASCIICHAT_OK);
  cr_assert_eq(terminal_fd_writer_write(writer, "last"), ASCIICHAT_OK);
  terminal_fd_writer_destroy(writer);
  writer = NULL;
  open_reader(2);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_not_null(frame);
  cr_assert_str_eq(frame, first);
  SAFE_FREE(frame);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_not_null(frame);
  cr_assert_eq(memcmp(frame, "last", 4), 0);
  cr_assert_eq(frame[4], '\0', "a partial final frame must also be terminated");
  SAFE_FREE(frame);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_null(frame);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_null(frame);
}

Test(terminal_fd_stream, unterminated_input_line_becomes_a_terminated_frame) {
  cr_assert_eq(platform_write(fd, "last line", 9), 9);
  open_reader(1);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_not_null(frame);
  cr_assert_eq(memcmp(frame, "last line", 9), 0);
  cr_assert_eq(frame[9], '\0');
  SAFE_FREE(frame);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_null(frame);
}

Test(terminal_fd_stream, empty_input_and_empty_frames_are_distinct) {
  open_reader(2);
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_null(frame);
  terminal_fd_reader_destroy(reader);
  reader = NULL;
  fd = platform_open("empty-frame-fixture", path, O_WRONLY | O_TRUNC);
  cr_assert_geq(fd, 0);
  cr_assert_eq(platform_write(fd, "\n\n", 2), 2);
  open_reader(1);
  for (int i = 0; i < 2; ++i) {
    cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
    cr_assert_not_null(frame);
    cr_assert_str_empty(frame);
    SAFE_FREE(frame);
  }
  cr_assert_eq(terminal_fd_reader_next(reader, &frame), ASCIICHAT_OK);
  cr_assert_null(frame);
}

Test(terminal_fd_stream, invalid_parameters_do_not_create_streams) {
  cr_assert_eq(terminal_fd_reader_create(fd, 1, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(terminal_fd_writer_create(fd, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(terminal_fd_reader_create(-1, 1, &reader), ERROR_INVALID_PARAM);
  cr_assert_eq(terminal_fd_reader_create(fd, 0, &reader), ERROR_INVALID_PARAM);
  cr_assert_eq(terminal_fd_reader_create(fd, -1, &reader), ERROR_INVALID_PARAM);
  cr_assert_null(reader);
  cr_assert_eq(terminal_fd_writer_create(-1, &writer), ERROR_INVALID_PARAM);
  cr_assert_null(writer);
  cr_assert_eq(terminal_fd_reader_next(NULL, &frame), ERROR_INVALID_PARAM);
  cr_assert_eq(terminal_fd_writer_write(NULL, "frame"), ERROR_INVALID_PARAM);
  terminal_fd_reader_destroy(NULL);
  terminal_fd_writer_destroy(NULL);
}
