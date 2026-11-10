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

renderer.configure({
  width,
  height,
  color_mode: ColorMode.None,
  render_mode: RenderMode.Foreground,
  renderer: TextRenderer.Luminance,
  stabilize: true,
});

function frame(time) {
  const rgba = renderer.testPattern(320, 240, 1, time);
  const art = renderer.render(rgba, 320, 240);
  output.textContent = art.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "");
  status.textContent = `Live WASM render · ${Math.round(time / 1000)}s`;
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
