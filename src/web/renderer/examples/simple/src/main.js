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

function ansiToTextGrid(ansi, columns, rows) {
  const grid = Array.from({ length: rows }, () => Array(columns).fill(" "));
  let row = 0;
  let column = 0;
  let index = 0;
  while (index < ansi.length) {
    const character = ansi[index++];
    if (character === "\x1b" && ansi[index] === "[") {
      index++;
      const start = index;
      while (index < ansi.length && !/[A-Za-z]/.test(ansi[index])) index++;
      const final = ansi[index++] ?? "";
      const params = ansi
        .slice(start, index - 1)
        .split(";")
        .map((value) => Number(value) || 0);
      if (final === "H" || final === "f") {
        row = Math.max(0, (params[0] || 1) - 1);
        column = Math.max(0, (params[1] || 1) - 1);
      } else if (final === "G") column = Math.max(0, (params[0] || 1) - 1);
      else if (final === "C") column += params[0] || 1;
      else if (final === "D") column = Math.max(0, column - (params[0] || 1));
      else if (final === "J") grid.forEach((line) => line.fill(" "));
      else if (final === "K") grid[row]?.fill(" ", column);
      continue;
    }
    if (character === "\r") column = 0;
    else if (character === "\n") {
      row++;
      column = 0;
    } else if (character >= " " && row < rows && column < columns)
      grid[row][column++] = character;
  }
  return grid.map((line) => line.join("").trimEnd()).join("\n");
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
const rgba = new Uint8Array(frameWidth * frameHeight * 4);

function frame(time) {
  const phase = time / 900;
  for (let y = 0; y < frameHeight; y++) {
    for (let x = 0; x < frameWidth; x++) {
      const wave = Math.sin(x / 18 + phase) + Math.cos(y / 11 - phase * 1.3);
      const glow = Math.max(
        0,
        1 - Math.hypot(x - frameWidth * 0.55, y - frameHeight * 0.5) / 180,
      );
      const value = Math.max(0, Math.min(255, 100 + wave * 45 + glow * 110));
      const offset = (y * frameWidth + x) * 4;
      rgba[offset] = value;
      rgba[offset + 1] = value * 0.75;
      rgba[offset + 2] = value * 0.35;
      rgba[offset + 3] = 255;
    }
  }
  const art = renderer.render(rgba, frameWidth, frameHeight);
  output.textContent = ansiToTextGrid(art, width, height);
  status.textContent = `Live WASM render · ${Math.round(time / 1000)}s`;
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
