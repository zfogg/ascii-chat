import {
  createRenderer,
  ColorMode,
  RenderMode,
  TextRenderer,
} from "ascii-chat";
import "./style.css";

const output = document.querySelector("#output");
const status = document.querySelector("#status");
let renderer;
try {
  renderer = await createRenderer({
    moduleUrl: "/ascii-chat-assets/renderer.js",
    wasmUrl: "/ascii-chat-assets/renderer.wasm",
    pthreadPoolSize: 0,
  });
} catch (error) {
  status.textContent = `Renderer failed: ${error instanceof Error ? error.message : String(error)}`;
  throw error;
}
const width = 96;
const height = 48;

function pixelsToAscii(pixels, sourceWidth, sourceHeight, columns, rows) {
  const palette = " .:-=+*#%@";
  return Array.from({ length: rows }, (_, row) => {
    const y = Math.min(
      sourceHeight - 1,
      Math.floor(((row + 0.5) * sourceHeight) / rows),
    );
    return Array.from({ length: columns }, (_, column) => {
      const x = Math.min(
        sourceWidth - 1,
        Math.floor(((column + 0.5) * sourceWidth) / columns),
      );
      const offset = (y * sourceWidth + x) * 4;
      const luminance =
        (pixels[offset] * 0.2126 +
          pixels[offset + 1] * 0.7152 +
          pixels[offset + 2] * 0.0722) /
        255;
      return palette[
        Math.min(palette.length - 1, Math.floor(luminance * palette.length))
      ];
    }).join("");
  }).join("\n");
}

renderer.configure({
  width,
  height,
  color_mode: ColorMode.None,
  render_mode: RenderMode.Foreground,
  renderer: TextRenderer.Luminance,
  stabilize: true,
});

const frameWidth = 320;
const frameHeight = 160;
function frame(time) {
  const rgba = renderer.testPattern(frameWidth, frameHeight, 0, time);
  output.textContent = pixelsToAscii(
    rgba,
    frameWidth,
    frameHeight,
    width,
    height,
  );
  status.textContent = `Live WASM render · ${Math.round(time / 1000)}s`;
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
