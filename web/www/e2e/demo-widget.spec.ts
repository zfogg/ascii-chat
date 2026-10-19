import { test, expect } from "@playwright/test";
import { fileURLToPath } from "node:url";
import type { MirrorModule } from "../../packages/shared/src/wasm/mirror";
declare global {
  interface Window {
    __demoProbe: {
      keys: number;
      splashes: number;
      paints: number;
      frames: string[];
    };
  }
}

test("demo playback ignores terminal shortcuts and never requests the splash", async ({
  page,
}) => {
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
  await page.goto("/");
  const canvas = page.locator("canvas.ascii-canvas").first();
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
    const probe = { keys: 0, splashes: 0, paints: 0, frames: [] as string[] };
    window.__demoProbe = probe;
    const key = m._wasm_ui_key.bind(m),
      loading = m._wasm_ui_loading.bind(m),
      feed = m._term_renderer_feed;
    m._wasm_ui_key = (...args: Parameters<MirrorModule["_wasm_ui_key"]>) => {
      probe.keys++;
      return key(...args);
    };
    m._wasm_ui_loading = (
      ...args: Parameters<MirrorModule["_wasm_ui_loading"]>
    ) => {
      probe.splashes += Number(Boolean(args[1]));
      return loading(...args);
    };
    m._term_renderer_feed = (r: number, p: number, n: number) => {
      probe.paints++;
      probe.frames.push(new TextDecoder().decode(m.HEAPU8.slice(p, p + n)));
      if (probe.frames.length > 20) probe.frames.shift();
      return feed(r, p, n);
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
  expect(probe.keys).toBe(0);
  expect(probe.splashes).toBe(0);
  expect(probe.frames.join("")).not.toMatch(
    /Keyboard Shortcuts|search logs|Video chat in your terminal/,
  );
  await expect
    .poll(() => page.evaluate(() => window.__demoProbe.paints))
    .toBeGreaterThan(probe.paints);
  await page.screenshot({ path: "artifacts/demo-passive.png" });
});
