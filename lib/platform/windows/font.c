/**
 * @file platform/windows/font.c
 * @ingroup platform
 * @brief Font resolution stub — render-file is not available on Windows
 */
#ifdef _WIN32
#include <ascii-chat/platform/font.h>

asciichat_error_t platform_font_resolve(const char *spec, char *out,
                                        size_t out_size, bool *out_is_path,
                                        const uint8_t **out_font_data,
                                        size_t *out_font_data_size) {
    (void)spec; (void)out; (void)out_size; (void)out_is_path;
    (void)out_font_data; (void)out_font_data_size;
    return SET_ERRNO(ERROR_UNSUPPORTED,
                     "platform_font_resolve: not available on Windows");
}
#endif
