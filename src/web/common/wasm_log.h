/**
 * @file wasm_log.h
 * @brief Shared logging macros for WASM modules
 * @ingroup web_common
 *
 * Provides unified WASM_LOG* macros that can be configured per-module.
 * Define WASM_LOG_USE_ERROR before including to use console.error instead of console.log.
 */

#pragma once

#include <emscripten.h>

#ifdef WASM_LOG_USE_ERROR
#define WASM_LOG(msg) EM_ASM({ console.error('[C] ' + UTF8ToString($0)); }, msg)
#define WASM_LOG_INT(msg, val) EM_ASM({ console.error('[C] ' + UTF8ToString($0) + ': ' + $1); }, msg, val)
#else
#define WASM_LOG(msg) EM_ASM({ console.log('[C] ' + UTF8ToString($0)); }, msg)
#define WASM_LOG_INT(msg, val) EM_ASM({ console.log('[C] ' + UTF8ToString($0) + ': ' + $1); }, msg, val)
#endif

#define WASM_ERROR(msg) EM_ASM({ console.error('[C] ERROR: ' + UTF8ToString($0)); }, msg)
