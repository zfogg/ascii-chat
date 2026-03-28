/**
 * @file main_wasm.c
 * @brief WASM entry point for both mirror and client modes
 *
 * This module provides the main() entry point for WASM builds, which calls
 * either mirror_main() or client_main() depending on the build-time define.
 *
 * Build configuration:
 * - mirror-web: Compiled with -DBUILD_WASM_MIRROR → calls mirror_main()
 * - client-web: Compiled with -DBUILD_WASM_CLIENT → calls client_main()
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <stdio.h>
#include <unistd.h>

// Forward declarations from native mode entry points
extern int mirror_main(void);
extern int client_main(void);

EM_JS(void, wasm_main_log, (const char *msg), {
  console.error('[WASM-MAIN] ' + UTF8ToString(msg));
});

/**
 * @brief Main entry point for WASM builds
 *
 * Called automatically by Emscripten as the program entry point.
 * Dispatches to either mirror_main() or client_main() based on compile-time define.
 *
 * The program runs in an Emscripten pthread (via -sPROXY_TO_PTHREAD=1),
 * allowing blocking operations like the render loop to work correctly.
 *
 * @return Exit status from the mode implementation
 */
int main(void) {
    wasm_main_log("main() ENTRY");
#ifdef BUILD_WASM_MIRROR
    wasm_main_log("main: about to call mirror_main");
    int result = mirror_main();
    wasm_main_log("main: mirror_main returned");
    return result;
#elif defined(BUILD_WASM_CLIENT)
    wasm_main_log("main: about to call client_main");
    int result = client_main();
    wasm_main_log("main: client_main returned");
    return result;
#else
    #error "Either BUILD_WASM_MIRROR or BUILD_WASM_CLIENT must be defined at compile time"
#endif
}

#endif // __EMSCRIPTEN__
