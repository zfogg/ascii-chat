#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
  printf("TTY Detection Test\n");
  printf("==================\n\n");

  printf("isatty(STDIN_FILENO=0):  %d\n", isatty(STDIN_FILENO));
  printf("isatty(STDOUT_FILENO=1): %d\n", isatty(STDOUT_FILENO));
  printf("isatty(STDERR_FILENO=2): %d\n", isatty(STDERR_FILENO));

  printf("\nstdin: %s\n", ttyname(STDIN_FILENO) ? ttyname(STDIN_FILENO) : "not a tty");
  printf("stdout: %s\n", ttyname(STDOUT_FILENO) ? ttyname(STDOUT_FILENO) : "not a tty");
  printf("stderr: %s\n", ttyname(STDERR_FILENO) ? ttyname(STDERR_FILENO) : "not a tty");

  printf("\nFile descriptor flags:\n");
  printf("stdin flags:  %d\n", fcntl(STDIN_FILENO, F_GETFL));
  printf("stdout flags: %d\n", fcntl(STDOUT_FILENO, F_GETFL));
  printf("stderr flags: %d\n", fcntl(STDERR_FILENO, F_GETFL));

  return 0;
}
