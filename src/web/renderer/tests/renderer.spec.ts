import { test, expect } from "@playwright/test";

test("standalone package renders modes, visualizations and canvas at 60 fps", async ({
  page,
}) => {
  await page.goto("/");
  const results = await page.evaluate(async () => {
    const url = "/renderer/src/index.js";
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
  expect(results.fps).toBeGreaterThan(50);
  console.log(
    `Standalone renderer: ${results.fps.toFixed(1)} fps, canvas ${results.canvas.join("x")}`,
  );
});
