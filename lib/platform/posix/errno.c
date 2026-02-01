/**
 * @file platform/posix/errno.c
 * @brief POSIX error handling implementation
 */

#include "../errno.h"
#include <errno.h>

void platform_clear_error_state(void) {
  /* POSIX: Clear system errno */
  errno = 0;
}
