import { test, expect, type Page } from "@playwright/test";
import { fileURLToPath } from "node:url";

const moduleUrl = `/@fs/${fileURLToPath(new URL("../../../packages/shared/src/wasm/mirror.ts", import.meta.url)).replaceAll("\\", "/")}`;

declare global {
  interface Window {
    __sessionUiFrames?: string[];
  }
}

async function observe(page: Page) {
  await page.goto("/mirror");
  await page.waitForFunction(() => {
    const canvas = document.querySelector<HTMLCanvasElement>(
      "canvas.ascii-canvas",
    );
    if (!canvas?.width || !canvas.height) return false;
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    return pixels.some((value, index) => index % 4 !== 3 && value > 100);
  });
  await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const m = getMirrorModule();
    const feed = m._term_renderer_feed;
    window.__sessionUiFrames = [];
    m._term_renderer_feed = (
      renderer: number,
      pointer: number,
      length: number,
    ) => {
      const text = m.UTF8ToString(pointer, length);
      window.__sessionUiFrames!.push(text);
      if (window.__sessionUiFrames!.length > 30)
        window.__sessionUiFrames!.shift();
      return feed(renderer, pointer, length);
    };
  }, moduleUrl);
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
}

async function lastFrame(page: Page) {
  return page.evaluate(() => window.__sessionUiFrames?.at(-1) ?? "");
}

test.use({ viewport: { width: 1440, height: 1000 } });

test("C splash animates and keyboard settings update both C and the web controls", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await observe(page);
  const canvas = page.locator("canvas.ascii-canvas");
  const before = await canvas.screenshot();
  await expect
    .poll(async () => (await canvas.screenshot()).equals(before))
    .toBe(false);
  await canvas.focus();
  await page.keyboard.press("?");
  await expect
    .poll(() => lastFrame(page))
    .toContain("ascii-chat Keyboard Shortcuts");
  await page.keyboard.press("c");
  await expect(page).toHaveURL(/colorMode=none/);
  await expect.poll(() => lastFrame(page)).toContain("Mono");
  for (const [key, parameter, value] of [
    ["x", "flipX", "false"],
    ["y", "flipY", "true"],
    ["r", "renderMode", "1"],
    ["1", "animationEnabled", "true"],
    ["2", "animation", "waveform"],
    ["3", "animation", "fft"],
  ]) {
    await page.keyboard.press(key!);
    await expect
      .poll(() => new URL(page.url()).searchParams.get(parameter!))
      .toBe(value);
  }
  await page.keyboard.press("ArrowDown");
  await expect.poll(() => lastFrame(page)).toMatch(/(?:89|90)%/);
  await page.keyboard.press("m");
  await expect.poll(() => lastFrame(page)).toMatch(/Volume\s*:\s*\[\s*\] 0%/);
  await page.keyboard.press("m");
  await expect.poll(() => lastFrame(page)).toMatch(/(?:89|90)%/);
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await expect(
    page
      .locator("select")
      .filter({ has: page.locator('option[value="truecolor"]') }),
  ).toHaveValue("none");
  await canvas.focus();
  await page.keyboard.press("Escape");
  await expect
    .poll(() => lastFrame(page))
    .toContain("Video chat in your terminal");
  expect(errors).toEqual([]);
});

test("help owns presentation during live video and recovers after resize", async ({
  page,
}) => {
  await observe(page);
  await page.getByRole("button", { name: "Start Webcam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  const canvas = page.locator("canvas.ascii-canvas");
  await canvas.focus();
  await page.keyboard.press("?");
  await expect
    .poll(() => lastFrame(page))
    .toContain("ascii-chat Keyboard Shortcuts");
  await page.waitForTimeout(400);
  const frames = await page.evaluate(() => window.__sessionUiFrames!.slice(-5));
  expect(
    frames.every((frame) => frame.includes("ascii-chat Keyboard Shortcuts")),
  ).toBe(true);
  await page.setViewportSize({ width: 480, height: 480 });
  await expect.poll(() => lastFrame(page)).toContain("Terminal too small");
  await page.setViewportSize({ width: 1440, height: 1000 });
  await expect
    .poll(() => lastFrame(page))
    .toContain("ascii-chat Keyboard Shortcuts");
  await canvas.focus();
  await page.keyboard.press("Escape");
  await expect
    .poll(async () => {
      const frame = await lastFrame(page);
      return (
        frame.length > 100 &&
        !frame.includes("Keyboard Shortcuts") &&
        !frame.includes("Video chat in your terminal")
      );
    })
    .toBe(true);
});

test("compiled C runtime retains newest covered frame, bounds input and tears down repeatedly", async ({
  page,
}) => {
  await observe(page);
  const result = await page.evaluate(async (url) => {
    const { getMirrorModule } = await import(url);
    const m = getMirrorModule();
    const text = (value: string, fn: (p: number, n: number) => void) => {
      const n = m.lengthBytesUTF8(value),
        p = m._malloc(n + 1);
      try {
        m.stringToUTF8(value, p, n + 1);
        fn(p, n);
      } finally {
        m._free(p);
      }
    };
    const outcomes = [];
    for (let i = 0; i < 10; i++) {
      const s = m._wasm_ui_create();
      m._wasm_ui_resize(s, 120, 40);
      text("old", (p, n) => m._wasm_ui_media(s, p, n));
      m._wasm_ui_key(s, 63);
      const help = m.UTF8ToString(m._wasm_ui_tick(s, 100));
      text("newest", (p, n) => m._wasm_ui_media(s, p, n));
      const covered = m.UTF8ToString(m._wasm_ui_tick(s, 200));
      m._wasm_ui_key(s, 27);
      const restored = m.UTF8ToString(m._wasm_ui_tick(s, 300));
      const unchanged = m._wasm_ui_tick(s, 400);
      const invalid = m._wasm_ui_media(s, 0, 0);
      outcomes.push(
        help.includes("Keyboard Shortcuts") &&
          covered.includes("Keyboard Shortcuts") &&
          restored.endsWith("newest") &&
          unchanged === 0 &&
          invalid !== 0,
      );
      m._wasm_ui_destroy(s);
    }
    return outcomes;
  }, moduleUrl);
  expect(result).toEqual(Array(10).fill(true));
});
