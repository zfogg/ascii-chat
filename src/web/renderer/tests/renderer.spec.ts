/// <reference types="node" />
import { test, expect } from "@playwright/test";
import { readFileSync } from "node:fs";

test("standalone package renders modes, visualizations and canvas at 60 fps", async ({
  page,
}) => {
  await page.goto("/");
  const results = await page.evaluate(async () => {
    const url = "/index.js";
    const { createRenderer, ColorMode, RenderMode, TextRenderer } =
      await import(url);
    const renderer = await createRenderer();
    const canvas = document.querySelector("canvas")!;
    try {
      const input = renderer.testPattern(160, 120, 1, 0);
      const modes: number[] = [];
      const frames: string[] = [];
      for (const color of [
        ColorMode.Auto,
        ColorMode.None,
        ColorMode.Ansi16,
        ColorMode.Ansi256,
        ColorMode.Truecolor,
      ])
        for (const render of [
          RenderMode.Foreground,
          RenderMode.Background,
          RenderMode.HalfBlock,
        ])
          for (const text of [TextRenderer.Luminance, TextRenderer.Glyph]) {
            renderer.configure({
              width: 40,
              height: 20,
              color_mode: color,
              render_mode: render,
              renderer: text,
              matrix_rain: false,
            });
            const output = renderer.render(input, 160, 120);
            if (!output.trim())
              throw new Error(`Empty frame: ${color},${render},${text}`);
            modes.push(output.length);
            frames.push(output);
          }
      const uniqueFrames = new Set(frames).size;
      renderer.options.palette("custom", " .:-=+*#%@");
      renderer.configure({
        color_mode: ColorMode.Truecolor,
        render_mode: RenderMode.Foreground,
        renderer: TextRenderer.Luminance,
        matrix_rain: true,
        zalgo: 3,
      });
      const rain = renderer.render(input, 160, 120);
      renderer.configure({ matrix_rain: false, waveform: true, fft: false });
      renderer.submitAudio(
        Float32Array.from({ length: 8192 }, (_, i) => Math.sin(i * 0.1)),
      );
      const waveform = renderer.renderAudio(40, 20);
      renderer.configure({ waveform: false, fft: true });
      const fft = renderer.renderAudio(40, 20);
      renderer.configure({ fft: false, width: 40, height: 20 });
      let count = 0,
        first = 0,
        last = 0;
      await new Promise<void>((resolve, reject) => {
        const tick = (time: number) => {
          try {
            if (!count) first = time;
            const pixels = renderer.testPattern(160, 120, 1, time);
            const art = renderer.render(pixels, 160, 120);
            renderer.draw(canvas, art, 40, 20);
            last = time;
            if (++count < 120) requestAnimationFrame(tick);
            else resolve();
          } catch (error) {
            reject(error);
          }
        };
        requestAnimationFrame(tick);
      });
      const image = canvas
        .getContext("2d")!
        .getImageData(0, 0, canvas.width, canvas.height).data;
      return {
        count,
        fps: ((count - 1) * 1000) / (last - first),
        modes: modes.length,
        rain: rain.length,
        waveform: waveform.length,
        fft: fft.length,
        pixels: image.some((v: number, i: number) => i % 4 !== 3 && v !== 0),
        uniqueFrames,
        canvas: [canvas.width, canvas.height],
      };
    } finally {
      renderer.dispose();
    }
  });
  expect(results.modes).toBe(30);
  expect(results.uniqueFrames).toBeGreaterThan(8);
  expect(results.rain).toBeGreaterThan(0);
  expect(results.waveform).toBeGreaterThan(0);
  expect(results.fft).toBeGreaterThan(0);
  expect(results.pixels).toBe(true);
  expect(results.count).toBe(120);
  expect(results.fps).toBeGreaterThanOrEqual(59);
  console.log(
    `Standalone renderer: ${results.fps.toFixed(1)} fps, canvas ${results.canvas.join("x")}`,
  );
});

test("Embind session owns native initialization and cleanup", async ({
  page,
}) => {
  await page.goto("/");
  const result = await page.evaluate(async () => {
    const url = "/renderer.js";
    const factory = (await import(url)).default;
    const module = await factory({
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    const owner = new module.RendererSessionBinding();
    const duplicate = new module.RendererSessionBinding();
    const first = owner.valid();
    const second = duplicate.valid();
    const error = duplicate.error();
    duplicate.delete();
    const stillActive = owner.valid() && module.readOptions() !== null;
    owner.delete();
    const replacement = new module.RendererSessionBinding();
    const restarted = replacement.valid();
    replacement.delete();
    return {
      first,
      second,
      error,
      stillActive,
      restarted,
      cleanupExposed: "mirror_cleanup" in module,
      initExposed: "initMirror" in module,
    };
  });
  expect(result).toEqual({
    first: true,
    second: false,
    error: expect.any(Number),
    stillActive: true,
    restarted: true,
    cleanupExposed: false,
    initExposed: false,
  });
  expect(result.error).not.toBe(0);
});

test("object options patches commit atomically and respect revisions", async ({
  page,
}) => {
  await page.goto("/");
  const result = await page.evaluate(async () => {
    const url = "/renderer.js";
    const factory = (await import(url)).default;
    const module = await factory({
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    });
    const session = new module.RendererSessionBinding();
    try {
      const before = module.readOptions();
      const rejected = module.patchOptions(
        { width: 45, height: 0 },
        before.revision,
      );
      const afterRejected = module.readOptions();
      const committed = module.patchOptions(
        { width: 45, height: 22 },
        before.revision,
      );
      const afterCommit = module.readOptions();
      const stale = module.patchOptions(
        { width: 46, height: 23 },
        before.revision,
      );
      const afterStale = module.readOptions();
      const badInput = module.patchOptions("not an object", -1);
      return {
        before,
        rejected,
        afterRejected,
        committed,
        afterCommit,
        stale,
        afterStale,
        badInput,
      };
    } finally {
      session.delete();
    }
  });
  expect(result.rejected).not.toBe(0);
  expect(result.afterRejected).toEqual(result.before);
  expect(result.committed).toBe(0);
  expect(result.afterCommit.values).toMatchObject({ width: 45, height: 22 });
  expect(result.afterCommit.revision).toBe(result.before.revision + 1);
  expect(result.stale).not.toBe(0);
  expect(result.afterStale).toEqual(result.afterCommit);
  expect(result.badInput).not.toBe(0);
});

test("shared rendering constants match the native definitions", async ({
  page,
}) => {
  const terminal = readFileSync(
    new URL(
      "../../../../include/ascii-chat/platform/terminal.h",
      import.meta.url,
    ),
    "utf8",
  );
  const palette = readFileSync(
    new URL(
      "../../../../include/ascii-chat/video/ascii/palette.h",
      import.meta.url,
    ),
    "utf8",
  );
  const options = readFileSync(
    new URL(
      "../../../../include/ascii-chat/options/options.h",
      import.meta.url,
    ),
    "utf8",
  );
  await page.goto("/");
  const values = await page.evaluate(async () => {
    const url = "/index.js";
    const {
      ColorMode,
      RenderMode,
      ColorFilter,
      TextRenderer,
      defaultPalettes,
      MAX_CUSTOM_PALETTE_BYTES,
    } = await import(url);
    return {
      ColorMode,
      RenderMode,
      ColorFilter,
      TextRenderer,
      defaultPalettes,
      MAX_CUSTOM_PALETTE_BYTES,
    };
  });
  const nativeNumber = (source: string, name: string) => {
    const match = source.match(new RegExp("\\b" + name + "\\s*=\\s*(-?\\d+)"));
    if (!match) throw new Error("Missing native constant " + name);
    return Number(match[1]);
  };
  for (const [key, name] of Object.entries({
    Auto: "AUTO",
    None: "NONE",
    Ansi16: "16",
    Ansi256: "256",
    Truecolor: "TRUECOLOR",
  }))
    expect(values.ColorMode[key]).toBe(
      nativeNumber(terminal, "TERM_COLOR_" + name),
    );
  for (const [key, name] of Object.entries({
    Foreground: "FOREGROUND",
    Background: "BACKGROUND",
    HalfBlock: "HALF_BLOCK",
  }))
    expect(values.RenderMode[key]).toBe(
      nativeNumber(terminal, "RENDER_MODE_" + name),
    );
  for (const [key, value] of Object.entries(values.ColorFilter))
    expect(value).toBe(
      nativeNumber(terminal, "COLOR_FILTER_" + key.toUpperCase()),
    );
  for (const [key, value] of Object.entries(values.TextRenderer))
    expect(value).toBe(
      nativeNumber(options, "ASCII_RENDERER_" + key.toUpperCase()),
    );
  const paletteLimit = options.match(
    /#define\s+OPTIONS_PALETTE_CUSTOM_BUFFER_SIZE\s+(\d+)/,
  );
  if (!paletteLimit) throw new Error("Missing native palette buffer size");
  expect(values.MAX_CUSTOM_PALETTE_BYTES).toBe(Number(paletteLimit[1]) - 1);
  for (const [key, value] of Object.entries(values.defaultPalettes)) {
    const match = palette.match(
      new RegExp(
        "#define PALETTE_CHARS_" + key.toUpperCase() + '\\s+"([^"]*)"',
      ),
    );
    if (!match) throw new Error("Missing native palette " + key);
    expect(value).toBe(match[1]);
  }
});

test("palette length matches the native UTF-8 limit", async ({ page }) => {
  await page.goto("/");
  const result = await page.evaluate(async () => {
    const url = "/index.js";
    const { createRenderer } = await import(url);
    const renderer = await createRenderer();
    try {
      renderer.options.palette("custom", "x".repeat(255));
      const saved = renderer.options.read();
      const errors = [];
      for (const palette of [
        "x".repeat(256),
        "🌈".repeat(64),
        "x".repeat(1025),
      ]) {
        try {
          renderer.options.palette("custom", palette);
          errors.push("");
        } catch (error) {
          errors.push(String(error));
        }
      }
      return { saved, after: renderer.options.read(), errors };
    } finally {
      renderer.dispose();
    }
  });
  expect(result.saved.values.palette_custom).toHaveLength(255);
  expect(result.after).toEqual(result.saved);
  expect(
    result.errors.every((error: string) => error.includes("Custom palette")),
  ).toBe(true);
});
