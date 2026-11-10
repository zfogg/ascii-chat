import {
  createRenderer,
  ColorMode,
  RenderMode,
  TextRenderer,
} from "ascii-chat";
import "./style.css";

const canvas = document.querySelector("#canvas");
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
  color_mode: ColorMode.Truecolor,
  render_mode: RenderMode.HalfBlock,
  renderer: TextRenderer.Glyph,
  stabilize: true,
});

function frame(time) {
  const rgba = renderer.testPattern(320, 240, 1, time);
  const art = renderer.render(rgba, 320, 240);
  renderer.draw(canvas, art, width, height);
  status.textContent = `Live WASM render · ${Math.round(time / 1000)}s`;
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
