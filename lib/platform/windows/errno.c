/**
 * @file platform/windows/errno.c
 * @brief Windows error handling implementation
 */

#include <ascii-chat/platform/errno.h>
#include <winsock2.h>
#include <errno.h>

void platform_clear_error_state(void) {
  /* Windows: Clear WSA errors */
  WSASetLastError(0);
  /* Also clear system errno */
  errno = 0;
}
