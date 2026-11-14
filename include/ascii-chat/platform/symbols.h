#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <ascii-chat/error.h>

/**
 * @brief Determine whether a module belongs to ascii-chat.
 *
 * @param module_name Module name or path to inspect.
 * @return `true` when the module is an ascii-chat binary; otherwise `false`.
 */
bool symbol_module_is_project(const char *module_name);

/**
 * @brief Format a source path for an ascii-chat diagnostic.
 *
 * @param module_name Module that owns the source path.
 * @param source_path Source path reported by the symbolizer.
 * @param output Destination buffer.
 * @param output_size Size of @p output in bytes.
 * @param external Receives whether the path belongs to an external module.
 * @param owned_dirs Optional NULL-terminated list of project-owned directories.
 * @return @ref ASCIICHAT_OK on success, or an error code when the arguments
 *         are invalid or the destination is too small.
 */
asciichat_error_t symbol_format_source_path(const char *module_name, const char *source_path, char *output,
                                            size_t output_size, bool *external, const char *const *owned_dirs);
