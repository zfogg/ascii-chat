/**
 * @file main_wasm.c
 * @brief WASM entry point that initializes the module without running a blocking loop
 *
 * IMPORTANT: Do NOT call mirror_main() or client_main()!
 * Those functions implement terminal-based rendering with blocking main loops
 * that expect TTY I/O and user input - they will hang in the browser.
 *
 * Instead, JavaScript:
 * 1. Calls mirror_init_with_args() to initialize the C module
 * 2. Calls mirror_convert_frame() for each video frame
 * 3. Manages the render loop with requestAnimationFrame
 *
 * This entry point satisfies the Emscripten linker requirement for main()
 * but doesn't actually do any blocking operations.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

/**
 * @brief Main entry point required by Emscripten
 *
 * Returns immediately without blocking.
 * Real initialization happens when JavaScript calls:
 * - mirror_init_with_args() or client_init_with_args()
 *
 * @return 0 (exit status)
 */
int main(void) {
    // Empty - Emscripten needs this to satisfy linker requirement
    // Actual initialization is driven by JavaScript calls to exported functions
    return 0;
}

#endif // __EMSCRIPTEN__
