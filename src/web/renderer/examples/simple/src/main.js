import {
  createRenderer,
  ColorMode,
  RenderMode,
  TextRenderer,
} from "ascii-chat";
import "./style.css";

const canvas = document.querySelector("#canvas");
const status = document.querySelector("#status");
const renderer = await createRenderer({
  moduleUrl: "/ascii-chat-assets/renderer.js",
  wasmUrl: "/ascii-chat-assets/renderer.wasm",
  pthreadPoolSize: 0,
});

const width = 96;
const height = 48;
const frameWidth = 320;
const frameHeight = 160;

renderer.configure({
  width,
  height,
  color_mode: ColorMode.Truecolor,
  render_mode: RenderMode.Foreground,
  renderer: TextRenderer.Glyph,
  stabilize: true,
});

function frame(time) {
  const rgba = renderer.testPattern(frameWidth, frameHeight, 0, time);
  const art = renderer.render(rgba, frameWidth, frameHeight);
  renderer.draw(canvas, art, width, height);
  status.textContent = `Live WASM render · ${Math.round(time / 1000)}s`;
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
