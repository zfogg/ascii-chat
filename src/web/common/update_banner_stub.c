/**
 * @file update_banner_stub.c
 * @brief Browser builds do not run the native update checker.
 */

#include <stdbool.h>

bool update_banner_is_checking(void) {
  return false;
}
