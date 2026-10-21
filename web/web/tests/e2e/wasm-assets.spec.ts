import { expect, test } from "./browser-fixture";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";

const version = createHash("sha256")
  .update(readFileSync("src/wasm/dist/manifest.json"))
  .digest("hex")
  .slice(0, 32);

test("client runtime initializes after its versioned module loads", async ({
  page,
}) => {
  await page.goto("/client");
  const diagnostics = await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const { initClientWasm, cleanupClientWasm, getClientWasmModule } = await import(
      /* @vite-ignore */ moduleUrl
    );
    await initClientWasm({ width: 80, height: 24 });
    const wasm = getClientWasmModule() as Record<string, unknown>;
    const diagnostics = {
      stacks: typeof wasm._errno_print_stacks,
      history: typeof wasm._errno_print_history,
      legacy: Object.keys(wasm).filter((name) => name.startsWith("_asciichat_errno_")),
    };
    cleanupClientWasm();
    return diagnostics;
  });
  expect(diagnostics).toEqual({ stacks: "function", history: "function", legacy: [] });
});

for (const [mode, url] of [
  ["mirror", "/mirror?test2&playing=true"],
  ["client", "/client"],
  ["client", "/discovery"],
] as const) {
  test(`${url} loads matching versioned JavaScript and WASM`, async ({
    page,
    context,
  }) => {
    const assets = new Set<string>();
    context.on("request", (request) => {
      const path = new URL(request.url()).pathname;
      if (path.startsWith("/wasm/")) assets.add(path);
    });
    await context.route(/\/wasm\/(client|mirror)\.(js|wasm)$/, (route) =>
      route.abort(),
    );
    await page.goto(url);
    if (mode === "mirror") {
      await expect
        .poll(() =>
          page.locator("canvas.ascii-canvas").evaluate((element) => {
            const canvas = element as HTMLCanvasElement;
            return canvas
              .getContext("2d")!
              .getImageData(0, 0, canvas.width, canvas.height)
              .data.some((value, index) => index % 4 !== 3 && value > 8);
          }),
        )
        .toBe(true);
    } else {
      await page.waitForFunction(
        () =>
          !!(window as unknown as { asciiChatWasm?: unknown }).asciiChatWasm,
      );
    }
    expect(assets).toContain(`/wasm/${version}/${mode}.js`);
    expect(assets).toContain(`/wasm/${version}/${mode}.wasm`);
    const other = mode === "mirror" ? "client" : "mirror";
    expect(assets).not.toContain(`/wasm/${version}/${other}.js`);
    expect(
      [...assets].every((path) => path.startsWith(`/wasm/${version}/`)),
    ).toBe(true);
  });
}

for (const mode of ["mirror", "client"] as const) {
  test(`${mode} raster worker loads the same versioned artifact pair`, async ({
    page,
    context,
  }) => {
    const assets = new Set<string>();
    context.on("request", (request) => {
      const path = new URL(request.url()).pathname;
      if (path.startsWith("/wasm/")) assets.add(path);
    });
    await page.goto(`/tests/e2e/raster-worker.html?mode=${mode}`);
    await page.waitForFunction(() => "render" in window);
    const result = await page.evaluate(() =>
      (
        window as unknown as {
          render(matrix: boolean): Promise<{ lit: number }>;
        }
      ).render(false),
    );
    expect(result.lit).toBeGreaterThan(100);
    expect(assets).toContain(`/wasm/${version}/${mode}.js`);
    expect(assets).toContain(`/wasm/${version}/${mode}.wasm`);
    expect(
      [...assets].every((path) => path.startsWith(`/wasm/${version}/`)),
    ).toBe(true);
  });
}
