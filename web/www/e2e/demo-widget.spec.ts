import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
import type { MirrorModule } from "../../packages/shared/src/wasm/mirror";
import type {
  MirrorFrameBinding,
  SessionUiBinding,
  TerminalRendererBinding,
} from "../../packages/shared/src/wasm/generated/mirror-bindings";
declare global {
  interface Window {
    __demoProbe: {
      keys: number;
      splashes: number;
      paints: number;
      frames: string[];
      fits: number[];
    };
  }
}

test("demo playback ignores terminal shortcuts and never requests the splash", async ({
  page,
}) => {
  test.setTimeout(60_000);
  const response = await page.request.get("/@vite/client");
  test.skip(
    !response.ok() || !(await response.text()).includes("WebSocket"),
    "Module instrumentation requires the Vite development server.",
  );
  // Serve the generated artifact directly on Windows, where the public symlink is a text file.
  await page.context().route(/\/wasm\/mirror\.(js|wasm)$/, (route) => {
    const name = new URL(route.request().url()).pathname.endsWith(".wasm")
      ? "mirror.wasm"
      : "mirror.js";
    return route.fulfill({
      headers: {
        "Cross-Origin-Embedder-Policy": "require-corp",
        "Cross-Origin-Opener-Policy": "same-origin",
      },
      path: fileURLToPath(
        new URL("../../web/src/wasm/dist/" + name, import.meta.url),
      ),
      contentType: name.endsWith(".wasm")
        ? "application/wasm"
        : "text/javascript",
    });
  });
  await page.goto("/", { waitUntil: "domcontentloaded" });
  const canvas = page.locator("canvas.ascii-canvas").first();
  await expect(canvas).toHaveCSS("object-fit", "contain");
  await expect(canvas).toHaveAttribute("tabindex", "-1");
  await expect(canvas).toHaveAttribute("aria-label", "ASCII demo");
  await page
    .getByRole("button", { name: "Demo Video", exact: true })
    .first()
    .click();
  await expect
    .poll(
      () =>
        page
          .locator("video")
          .first()
          .evaluate((v: HTMLVideoElement) => v.currentTime),
      { timeout: 20000 },
    )
    .toBeGreaterThan(0);
  const moduleUrl =
    "/@fs/" +
    fileURLToPath(
      new URL("../../packages/shared/src/wasm/mirror.ts", import.meta.url),
    ).replaceAll("\\", "/");
  await page.evaluate(async (url) => {
    const m: MirrorModule = (await import(url)).getMirrorModule();
    const probe = {
      keys: 0,
      splashes: 0,
      paints: 0,
      frames: [] as string[],
      fits: [] as number[],
    };
    window.__demoProbe = probe;
    const frame = (
      m.MirrorFrameBinding as unknown as { prototype: MirrorFrameBinding }
    ).prototype;
    const convert = frame.convert;
    frame.convert = function (...args) {
      probe.fits.push(Number(args[3]));
      return convert.call(this, ...args);
    };
    const ui = (
      m.SessionUiBinding as unknown as { prototype: SessionUiBinding }
    ).prototype;
    const key = ui.key,
      loading = ui.loading;
    ui.key = function (...args) {
      probe.keys++;
      return key.call(this, ...args);
    };
    ui.loading = function (...args) {
      probe.splashes += Number(args[0]);
      return loading.call(this, ...args);
    };
    const terminal = (
      m.TerminalRendererBinding as unknown as {
        prototype: TerminalRendererBinding;
      }
    ).prototype;
    const feed = terminal.feed;
    terminal.feed = function (text) {
      probe.paints++;
      probe.frames.push(
        typeof text === "string" ? text : new TextDecoder().decode(text),
      );
      if (probe.frames.length > 20) probe.frames.shift();
      return feed.call(this, text);
    };
  }, moduleUrl);
  await expect
    .poll(() => page.evaluate(() => window.__demoProbe.paints))
    .toBeGreaterThan(3);
  const before = await canvas.screenshot();
  await expect
    .poll(async () => (await canvas.screenshot()).equals(before))
    .toBe(false);
  await canvas.focus();
  for (const key of [
    "]",
    "?",
    "c",
    "m",
    "x",
    "y",
    "r",
    "1",
    "2",
    "3",
    "ArrowDown",
    "Escape",
    " ",
  ])
    await page.keyboard.press(key);
  const probe = await page.evaluate(() => window.__demoProbe);
  expect(probe.fits.length).toBeGreaterThan(0);
  expect(new Set(probe.fits)).toEqual(new Set([1]));
  expect(probe.keys).toBe(0);
  expect(probe.splashes).toBe(0);
  expect(probe.frames.join("")).not.toMatch(
    /Keyboard Shortcuts|search logs|Video chat in your terminal/,
  );
  await expect
    .poll(() => page.evaluate(() => window.__demoProbe.paints))
    .toBeGreaterThan(probe.paints);
  await page.screenshot({ path: "artifacts/demo-passive.png" });
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  const fitted = await page.evaluate(async (url) => {
    const m = await import(url);
    const module = m.getMirrorModule();
    if (
      !module ||
      module.patchOptions(
        {
          width: 80,
          height: 40,
          render_mode: 2,
          color_mode: 3,
          flip_x: false,
          flip_y: false,
        },
        -1,
      )
    )
      throw new Error("Cannot update demo options");
    const rgba = new Uint8Array(8 * 4 * 4);
    for (let y = 0; y < 4; y++)
      for (let x = 0; x < 8; x++) {
        const color =
          x < 2
            ? [255, 0, 0, 255]
            : x >= 6
              ? [0, 255, 0, 255]
              : [0, 0, 255, 255];
        rgba.set(color, (y * 8 + x) * 4);
      }
    return {
      contain: m.convertFrameToAscii(rgba, 8, 4),
      cover: m.convertFrameToAscii(rgba, 8, 4, "cover"),
    };
  }, moduleUrl);
  for (const color of ["255;0;0", "0;255;0"]) {
    expect(fitted.contain).toContain(color);
    expect(fitted.cover).not.toContain(color);
  }
});
