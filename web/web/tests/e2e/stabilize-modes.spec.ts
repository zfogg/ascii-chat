import { fileURLToPath } from "node:url";
import { expect, test } from "./browser-fixture";

const moduleUrl = (path: string) =>
  `/@fs/${fileURLToPath(new URL(path, import.meta.url))
    .replaceAll("\\", "/")
    .replace(/^\/+/, "")}`;

test("mirror WASM stabilizes every render mode", async ({ page }) => {
  await page.goto("/mirror");
  await page.waitForFunction(() => {
    const canvas = document.querySelector<HTMLCanvasElement>(
      "canvas.ascii-canvas",
    );
    return !!canvas?.width && !!canvas.height;
  });
  await expect
    .poll(() =>
      page.evaluate(async (url) => {
        const { getMirrorModule } = await import(url);
        return !!getMirrorModule();
      }, moduleUrl("../../../packages/shared/src/wasm/mirror.ts")),
    )
    .toBe(true);
  const result = await page.evaluate(
    async ([mirrorUrl, optionsUrl]) => {
      const { getMirrorModule } = await import(mirrorUrl!);
      const { runtimeOptions } = await import(optionsUrl!);
      const m = getMirrorModule();
      if (!m) throw new Error("Mirror module was not initialized");
      const options = runtimeOptions(m);
      const frame = new m.MirrorFrameBinding();
      const pixels = new Uint8Array(4 * 4 * 4);
      try {
        const convert = (value: number, background: boolean) => {
          for (let i = 0; i < 16; i++) {
            pixels.set([value, value, value, 255], i * 4);
          }
          const ansi = frame.convert(pixels, 4, 4, false);
          if (!ansi) throw new Error("WASM conversion failed");
          const match = ansi.match(
            background ? /\x1b\[48;2;(\d+);/ : /\x1b\[38;2;(\d+);/,
          );
          if (!match) throw new Error("Missing truecolor output");
          return Number(match[1]);
        };
        const results = [];
        for (const renderer of [0, 1]) {
          for (const renderMode of [0, 1, 2]) {
            const sample = (enabled: boolean) => {
              options.applyPatch({
                width: 20,
                height: 10,
                color_mode: 3,
                color_filter: 0,
                render_mode: renderMode,
                renderer,
                stabilize: enabled,
              });
              const frame = new m.MirrorFrameBinding();
              frame.reset();
              frame.delete();
              return [
                convert(100, renderMode === 1),
                convert(104, renderMode === 1),
              ];
            };
            results.push({
              renderer,
              renderMode,
              raw: sample(false),
              stable: sample(true),
            });
          }
        }
        m.sessionRenderCleanup();
        m.sessionRenderCleanup();
        // Cleanup is per session: the shared options and module remain usable.
        options.applyPatch({ stabilize: false });
        if (convert(104, false) !== 104)
          throw new Error("Session cleanup broke the shared engine");
        return results;
      } finally {
        frame.delete();
      }
    },
    [
      moduleUrl("../../../packages/shared/src/wasm/mirror.ts"),
      moduleUrl("../../../packages/shared/src/wasm/common/runtimeOptions.ts"),
    ],
  );
  expect(result).toHaveLength(6);
  for (const { raw, stable } of result) {
    expect(raw[1]).toBeGreaterThan(raw[0]!);
    expect(stable[0]).toBe(raw[0]);
    expect(stable[1]).toBeGreaterThanOrEqual(raw[0]!);
    expect(stable[1]).toBeLessThan(raw[1]!);
  }
});
