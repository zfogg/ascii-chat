#pragma once

#include <stdbool.h>

/**
 * @brief Determine whether a module belongs to ascii-chat.
 *
 * @param module_name Module name or path to inspect.
 * @return `true` when the module is an ascii-chat binary; otherwise `false`.
 */
bool symbol_module_is_project(const char *module_name);
