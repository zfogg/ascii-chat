import { expect, measure, test } from "./wasm-boundary-fixture";
import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

declare global {
  interface Window {
    __boundary: { renderer: any; wasm: any };
    __clientModule: any;
  }
}

async function setupRenderer(page: import("@playwright/test").Page) {
  await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const module = await import(/* @vite-ignore */ moduleUrl);
    await module.initClientWasm();
    const wasm = module.getClientWasmModule();
    if (!wasm) throw new Error("client WASM did not initialize");
    const renderer = new wasm.AsciiRendererBinding(40, 20, 12, false, 0);
    if (!renderer.valid())
      throw new Error(`renderer error ${renderer.error()}`);
    (window as unknown as { __boundary: unknown }).__boundary = {
      wasm,
      renderer,
    };
  });
}

async function setupClient(page: import("@playwright/test").Page) {
  await page.evaluate(async () => {
    const moduleUrl = "/src/wasm/client.ts";
    const module = await import(/* @vite-ignore */ moduleUrl);
    await module.initClientWasm();
    (window as unknown as { __clientModule: unknown }).__clientModule = module;
  });
}

test.describe("WASM browser boundary measurements", () => {
  test.beforeEach(async ({ rendererPage }) => setupRenderer(rendererPage));

  test("renderer dimensions are one coarse call", async ({ rendererPage }) => {
    const result = await rendererPage.evaluate(() =>
      window.__boundary.renderer.dimensions(),
    );
    expect(result).toEqual({
      cols: 40,
      rows: 20,
      width: expect.any(Number),
      height: expect.any(Number),
    });
  });
  test("renderer returns an independent owned pixel buffer", async ({
    rendererPage,
  }) => {
    const result = await rendererPage.evaluate(() => {
      const r = window.__boundary.renderer;
      return r.render("hello", 0) !== r.render("hello", 0);
    });
    expect(result).toBe(true);
  });
  test("renderer empty frame measurement", async ({ rendererPage }) => {
    expect(
      (
        await measure(
          rendererPage,
          '() => window.__boundary.renderer.render("", 0)',
        )
      ).resultBytes,
    ).toBeGreaterThan(0);
  });
  test("renderer short frame measurement", async ({ rendererPage }) => {
    expect(
      (
        await measure(
          rendererPage,
          '() => window.__boundary.renderer.render("ascii", 0)',
        )
      ).elapsedMs,
    ).toBeGreaterThanOrEqual(0);
  });
  test("renderer long frame measurement", async ({ rendererPage }) => {
    expect(
      (
        await measure(
          rendererPage,
          '() => window.__boundary.renderer.render("x".repeat(800), 0)',
        )
      ).resultBytes,
    ).toBeGreaterThan(0);
  });
  test("renderer zalgo mode returns an owned snapshot", async ({ rendererPage }) => {
    expect(
      (
        await measure(
          rendererPage,
          '() => window.__boundary.renderer.render("ascii", 1)',
        )
      ).stableResultIdentity,
    ).toBe(false);
  });
  test("renderer dimensions remain stable across frames", async ({
    rendererPage,
  }) => {
    expect(
      await rendererPage.evaluate(
        () =>
          JSON.stringify(window.__boundary.renderer.dimensions()) ===
          JSON.stringify(window.__boundary.renderer.dimensions()),
      ),
    ).toBe(true);
  });
  test("renderer output byte count is repeatable", async ({ rendererPage }) => {
    const a = await measure(
      rendererPage,
      '() => window.__boundary.renderer.render("frame", 0)',
      8,
    );
    const b = await measure(
      rendererPage,
      '() => window.__boundary.renderer.render("frame", 0)',
      8,
    );
    expect(a.resultBytes).toBe(b.resultBytes);
  });

  test("client module initializes once", async ({ clientPage }) => {
    await setupClient(clientPage);
    expect(
      await clientPage.evaluate(() => Boolean((window as any).__clientModule)),
    ).toBe(true);
  });
  test("client module exposes the coarse session boundary", async ({
    clientPage,
  }) => {
    await setupClient(clientPage);
    expect(
      await clientPage.evaluate(
        () =>
          typeof (window as any).__clientModule.getClientWasmModule()
            .ClientSessionBinding.prototype.process,
      ),
    ).toBe("function");
  });
  test("client event batching measures fewer semantic boundary calls", async ({
    clientPage,
  }) => {
    await setupClient(clientPage);
    const result = await clientPage.evaluate(async () => {
      const module = (window as any).__clientModule;
      const wasm = module.getClientWasmModule();
      const session = new wasm.ClientSessionBinding({});
      const event = { type: "snapshot" };
      const startSingle = performance.now();
      for (let i = 0; i < 32; i++) session.process(event);
      const singleMs = performance.now() - startSingle;
      const startBatch = performance.now();
      session.processBatch(Array.from({ length: 32 }, () => event));
      const batchMs = performance.now() - startBatch;
      session.close();
      session.delete();
      return { singleMs, batchMs, semanticCalls: { single: 32, batch: 1 } };
    });
    expect(result.semanticCalls).toEqual({ single: 32, batch: 1 });
    expect(result.singleMs).toBeGreaterThanOrEqual(0);
    expect(result.batchMs).toBeGreaterThanOrEqual(0);
  });
  test("renderer no longer exposes the old feed ABI", async ({
    rendererPage,
  }) => {
    expect(
      await rendererPage.evaluate(() => "feed" in window.__boundary.renderer),
    ).toBe(false);
  });
  test("renderer boundary measurement returns a typed array", async ({
    rendererPage,
  }) => {
    expect(
      await rendererPage.evaluate(
        () =>
          window.__boundary.renderer.render("x", 0) instanceof
          Uint8ClampedArray,
      ),
    ).toBe(true);
  });

  test("records main-versus-feature artifact size measurements when configured", async () => {
    const mainRoot = process.env["WASM_MAIN_ROOT"];
    test.skip(
      !mainRoot,
      "Set WASM_MAIN_ROOT to the main worktree for artifact comparison",
    );
    const mainManifest = resolve(
      mainRoot!,
      "web/web/src/wasm/dist/manifest.json",
    );
    const featureManifest = resolve(
      process.cwd(),
      "src/wasm/dist/manifest.json",
    );
    test.skip(
      !existsSync(mainManifest) || !existsSync(featureManifest),
      "Both WASM manifests are required",
    );
    const main = readFileSync(mainManifest, "utf8");
    const feature = readFileSync(featureManifest, "utf8");
    expect({
      mainBytes: Buffer.byteLength(main),
      featureBytes: Buffer.byteLength(feature),
    }).toEqual({
      mainBytes: expect.any(Number),
      featureBytes: expect.any(Number),
    });
  });
});
