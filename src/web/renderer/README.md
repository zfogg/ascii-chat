# ascii-chat renderer for JavaScript

The `ascii-chat` package converts RGBA frames to ANSI ASCII art with the same
libasciichat renderer as the browser client. It also rasterizes that art to a
canvas, renders audio waveforms and FFTs, and generates animated test patterns.
It has no network connection to ascii-chat servers.

```ts
import {
  createRenderer,
  ColorMode,
  RenderMode,
  TextRenderer,
} from "ascii-chat";

const renderer = await createRenderer();
renderer.configure({
  width: 80,
  height: 40,
  color_mode: ColorMode.Truecolor,
  render_mode: RenderMode.HalfBlock,
  renderer: TextRenderer.Glyph,
  stabilize: true,
});
const rgba = renderer.testPattern(320, 240, 1, performance.now());
const art = renderer.render(rgba, 320, 240);
renderer.draw(document.querySelector("canvas")!, art, 80, 40);
// art is a JavaScript-owned ANSI string; you can instead use any terminal,
// parse it yourself, or send it to your own rendering code.
renderer.dispose();
```

Call `render` for every input frame; it accepts tightly packed RGBA bytes as a
`Uint8Array` or `Uint8ClampedArray` and copies them into WASM. Alpha is ignored.
`draw` uses libvterm and FreeType to paint to a 2D canvas; it retains its raster
renderer across calls. Pass matching output columns and rows to `draw` and the
`width` and `height` options. `reset()` clears the temporal stabilizer when the
source changes. `dispose()` releases the frame, test-pattern, raster, and Embind-owned session.
The session destructor performs the native cleanup; `dispose()` is safe to call
more than once. Each `createRenderer()`
creates an independent module. Do not use a disposed renderer.

## Settings and effects

`renderer.options.read()` returns `{revision, values}`. `configure(values)` or
`renderer.options.patch(values, expectedRevision)` applies the same validated
runtime settings used by the browser client; `expectedRevision` is optional.
Use `renderer.options.palette("custom", " .:-=+*#%@")` for a custom palette,
or select `standard`, `blocks`, `digital`, `minimal`, or `cool`. Options
include `width`, `height`, `color_mode`, `render_mode`, `renderer`,
`color_filter`, `flip_x`, `flip_y`, `stabilize`, `fps`, `matrix_rain`, `zalgo`,
`waveform`, and `fft`. `ColorMode`, `RenderMode`, `TextRenderer`, and
`ColorFilter` export the corresponding numeric constants. The options API
exposes the shared runtime option registry; options unrelated to rendering do
not add capture, media decoding, or networking to this package.

For visualizations, submit mono samples and render the selected visualization:

```ts
renderer.configure({ waveform: true, fft: false });
renderer.submitAudio(samples); // Float32Array, media source by default
const waveform = renderer.renderAudio(80, 40);
renderer.configure({ waveform: false, fft: true });
const spectrum = renderer.renderAudio(80, 40);
```

Pass `"microphone"` as the last argument to use its separate audio history.
Matrix rain and rainbow filtering advance when frames are rendered. The package
does not schedule frames; use `requestAnimationFrame` or your own scheduler.

## Browser requirements and hosting

The module uses WASM SIMD and pthreads. Serve the page with
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`, and serve the WASM as
`application/wasm`. Cross-origin resources must also satisfy COEP. Use HTTPS
(or localhost) for the browser's secure-context APIs. The ESM loader resolves
`renderer.wasm` relative to its generated `renderer.js`. If a bundler or CDN
moves that asset, provide `createRenderer({ locateFile })` and return its URL
for `renderer.wasm`; make the generated JS accessible for pthread workers too.
The package targets browsers and workers, while `draw` specifically needs an
HTML canvas on the main thread.

## Building and testing

Configure the repository's Emscripten build as described in the main WASM
build guide, then build `renderer-web` (the parent build exposes this target
too). `npm ci` installs package development dependencies. In this directory:

```sh
cmake --build ../../../build-wasm --target renderer-web
npm ci
# Set ASCII_CHAT_WASM_ARTIFACT_DIR to an absolute path for another build dir.
npm run build
npm test
npm pack --dry-run
```

The build copies `renderer.js` and `renderer.wasm` from the build tree to
ignored `dist/`, alongside compiled TypeScript and declarations. The npm pack
includes those generated files; none are committed. Tests serve the package
from an isolated local HTTP server and render a test pattern through WASM to a
canvas for 120 animation frames, in addition to mode and visualization checks.
`CHROME_PATH` can select a locally installed Chromium executable.

The package includes the project MIT license and third-party notices in
`licenses/`.

For bundlers that relocate dependencies, explicitly copy both generated assets
and pass their URLs (for example, with Vite's `?url` asset imports):

```ts
import rendererUrl from "ascii-chat/renderer.js?url";
import wasmUrl from "ascii-chat/renderer.wasm?url";
const renderer = await createRenderer({
  moduleUrl: rendererUrl,
  locateFile: (path, prefix) =>
    path.endsWith(".wasm") ? wasmUrl : prefix + path,
});
```
