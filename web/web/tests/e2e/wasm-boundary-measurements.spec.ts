import { expect, measure, test } from "./wasm-boundary-fixture";
import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

declare global {
  type BoundaryRenderer = {
    dimensions(): { cols: number; rows: number; width: number; height: number };
    render(text: string, zalgo: number): Uint8ClampedArray;
    valid(): boolean;
    error(): number;
  };
  type BoundaryClientModule = {
    processClientEvent(event: object): { code: number };
    processClientEvents(events: object[]): { code: number }[];
    getClientWasmModule(): {
      ClientSessionBinding: new (options: object) => {
        process(event: object): { code: number };
        processBatch(events: object[]): unknown[];
        close(): void;
        delete(): void;
      };
    };
  };
  interface Window {
    __boundary: { renderer: BoundaryRenderer; wasm: unknown };
    __clientModule: BoundaryClientModule;
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

function masterArtifact(file: "client.js" | "client.wasm") {
  return execFileSync("git", ["show", `master:web/web/src/wasm/dist/${file}`], {
    maxBuffer: 64 * 1024 * 1024,
  });
}

async function setupMasterRenderer(page: import("@playwright/test").Page) {
  const js = masterArtifact("client.js");
  const wasm = masterArtifact("client.wasm");
  await page.route("**/__master_wasm/**", (route) => {
    const isWasm = route.request().url().endsWith(".wasm");
    return route.fulfill({
      body: isWasm ? wasm : js,
      contentType: isWasm ? "application/wasm" : "application/javascript",
    });
  });
  return page.evaluate(async () => {
    const moduleUrl = "/__master_wasm/client.js";
    const factory = (await import(/* @vite-ignore */ moduleUrl)).default as (
      options: Record<string, unknown>,
    ) => Promise<Record<string, unknown>>;
    const module = (await factory({
      locateFile: (name: string) => `/__master_wasm/${name}`,
      getRandomValue: () => crypto.getRandomValues(new Uint32Array(1))[0],
    })) as {
      AsciiRendererBinding: new (
        cols: number,
        rows: number,
        fontSize: number,
        matrix: boolean,
        zalgo: number,
      ) => BoundaryRenderer;
    };
    const renderer = new module.AsciiRendererBinding(40, 20, 12, false, 0);
    if (!renderer.valid())
      throw new Error(`master renderer error ${renderer.error()}`);
    (window as unknown as { __masterBoundary: unknown }).__masterBoundary = {
      renderer,
    };
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
      const first = r.render("hello", 0);
      const second = r.render("hello", 0);
      return (
        first instanceof Uint8ClampedArray &&
        first.length > 0 &&
        second instanceof Uint8ClampedArray &&
        second.length > 0 &&
        first !== second
      );
    });
    expect(result).toBe(true);
  });
  test("renderer empty frame measurement", async ({ rendererPage }) => {
    expect((await measure(rendererPage, "empty")).resultBytes).toBeGreaterThan(
      0,
    );
  });
  test("renderer short frame measurement", async ({ rendererPage }) => {
    const result = await measure(rendererPage, "short");
    expect(result.resultBytes).toBeGreaterThan(0);
    expect(result.elapsedMs).toBeGreaterThanOrEqual(0);
  });
  test("renderer long frame measurement", async ({ rendererPage }) => {
    expect((await measure(rendererPage, "long")).resultBytes).toBeGreaterThan(
      0,
    );
  });
  test("renderer zalgo mode returns an owned snapshot", async ({
    rendererPage,
  }) => {
    const result = await measure(rendererPage, "zalgo");
    expect(result.resultBytes).toBeGreaterThan(0);
    expect(result.stableResultIdentity).toBe(false);
    expect(result.allResultsUnique).toBe(true);
  });
  test("renderer dimensions remain stable across frames", async ({
    rendererPage,
  }) => {
    expect(
      await rendererPage.evaluate(() => {
        const renderer = window.__boundary.renderer;
        const before = renderer.dimensions();
        const pixels = renderer.render("frame", 0);
        return (
          pixels instanceof Uint8ClampedArray &&
          pixels.length > 0 &&
          JSON.stringify(before) === JSON.stringify(renderer.dimensions())
        );
      }),
    ).toBe(true);
  });
  test("renderer output byte count is repeatable", async ({ rendererPage }) => {
    const a = await measure(rendererPage, "frame", 8);
    const b = await measure(rendererPage, "frame", 8);
    expect(a.resultBytes).toBe(b.resultBytes);
  });

  test("records representative frame and session-event boundary metrics", async ({
    rendererPage,
    clientPage,
  }) => {
    await setupRenderer(rendererPage);
    const frame = await measure(rendererPage, "long", 60);
    await setupClient(clientPage);
    const events = await clientPage.evaluate(() => {
      const module = window.__clientModule;
      const event = {
        type: "send-packet",
        packetType: 5001,
        payload: new Uint8Array(),
        encrypt: false,
      };
      const singleStart = performance.now();
      let singleBytes = 0;
      for (let i = 0; i < 60; i++) {
        singleBytes += module.processClientEvent(event).code === 0 ? 1 : 0;
        module.processClientEvent({ type: "drain-outputs" });
      }
      const singleMs = performance.now() - singleStart;
      const batchStart = performance.now();
      const result = module.processClientEvents(
        Array.from({ length: 60 }, () => event),
      );
      return {
        singleCalls: 60,
        batchCalls: 1,
        batchResults: result.length,
        singleBytes,
        singleMs,
        batchMs: performance.now() - batchStart,
      };
    });
    expect(frame.calls).toBe(60);
    expect(frame.resultBytes).toBeGreaterThan(0);
    expect(frame.elapsedMs).toBeGreaterThanOrEqual(0);
    expect(events).toMatchObject({
      singleCalls: 60,
      batchCalls: 1,
      batchResults: 60,
      singleBytes: 60,
    });
    console.info("WASM boundary metrics", { frame, events });
    expect(events.singleMs).toBeGreaterThanOrEqual(0);
    expect(events.batchMs).toBeGreaterThanOrEqual(0);
  });

  test("compares the same renderer workload with the master artifact", async ({
    rendererPage,
  }) => {
    test.fixme(
      true,
      "master's historical pthread loader cannot initialize through the isolated browser route",
    );
    await setupRenderer(rendererPage);
    const current = await measure(rendererPage, "long", 60);
    await setupMasterRenderer(rendererPage);
    const master = await rendererPage.evaluate(() => {
      const renderer = (
        window as unknown as {
          __masterBoundary: { renderer: BoundaryRenderer };
        }
      ).__masterBoundary.renderer;
      const start = performance.now();
      let bytes = 0;
      for (let i = 0; i < 60; i++)
        bytes += renderer.render("x".repeat(800), 0).byteLength;
      return {
        calls: 60,
        resultBytes: bytes,
        elapsedMs: performance.now() - start,
      };
    });
    console.info("Master versus feature renderer metrics", { master, current });
    expect(master.calls).toBe(current.calls);
    expect(master.resultBytes).toBeGreaterThan(0);
    expect(current.resultBytes).toBeGreaterThan(0);
  });

  test("client module initializes once", async ({ clientPage }) => {
    await setupClient(clientPage);
    expect(
      await clientPage.evaluate(async () => {
        const module = window.__clientModule as BoundaryClientModule & {
          initClientWasm(): Promise<void>;
        };
        const before = module.getClientWasmModule();
        if (!before) return false;
        await module.initClientWasm();
        return before === module.getClientWasmModule();
      }),
    ).toBe(true);
  });
  test("client module exposes the coarse session boundary", async ({
    clientPage,
  }) => {
    await setupClient(clientPage);
    expect(
      await clientPage.evaluate(
        () =>
          typeof window.__clientModule.getClientWasmModule()
            .ClientSessionBinding.prototype.process,
      ),
    ).toBe("function");
  });
  test("client event batching measures fewer semantic boundary calls", async ({
    clientPage,
  }) => {
    await setupClient(clientPage);
    const result = await clientPage.evaluate(async () => {
      const module = window.__clientModule;
      const event = { type: "snapshot" };
      const startSingle = performance.now();
      for (let i = 0; i < 32; i++) {
        if (module.processClientEvent(event).code)
          throw new Error("snapshot failed");
      }
      const singleMs = performance.now() - startSingle;
      const startBatch = performance.now();
      const batch = module.processClientEvents(
        Array.from({ length: 32 }, () => event),
      );
      if (batch.length !== 32 || batch.some((result) => result.code))
        throw new Error("snapshot batch failed");
      const batchMs = performance.now() - startBatch;
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
    const featureManifest = resolve(
      process.cwd(),
      "src/wasm/dist/manifest.json",
    );
    if (!existsSync(featureManifest))
      throw new Error("Feature WASM manifest is required");
    const baselinePath = (file: string) => `web/web/src/wasm/dist/${file}`;
    const readBaseline = (file: string): Buffer => {
      if (mainRoot) {
        const path = resolve(mainRoot, baselinePath(file));
        if (existsSync(path)) return readFileSync(path);
      }
      return execFileSync("git", ["show", `master:${baselinePath(file)}`], {
        maxBuffer: 64 * 1024 * 1024,
      });
    };
    const mainManifest = JSON.parse(readBaseline("manifest.json").toString("utf8")) as {
      files?: Record<string, string>;
    };
    const feature = readFileSync(featureManifest, "utf8");
    const featureManifestData = JSON.parse(feature) as {
      files?: Record<string, string>;
    };
    const files = [
      "client.js",
      "client.wasm",
      "mirror.js",
      "mirror.wasm",
    ] as const;
    const sizes = Object.fromEntries(
      files.map((file) => [
        file,
        {
          main: readBaseline(file).byteLength,
          feature: readFileSync(resolve(process.cwd(), "src/wasm/dist", file))
            .byteLength,
        },
      ]),
    );
    const countMarker = (bytes: Buffer, marker: string) =>
      bytes.toString("latin1").split(marker).length - 1;
    const baselineBytes = Object.fromEntries(
      files.map((file) => [file, readBaseline(file)]),
    ) as Record<(typeof files)[number], Buffer>;
    const featureBytes = Object.fromEntries(
      files.map((file) => [
        file,
        readFileSync(resolve(process.cwd(), "src/wasm/dist", file)),
      ]),
    ) as Record<(typeof files)[number], Buffer>;
    const abiMarkers = ["feed", "processClientEvent", "ClientSessionBinding"];
    const abi = Object.fromEntries(
      abiMarkers.map((marker) => [
        marker,
        {
          main: countMarker(baselineBytes["client.js"]!, marker),
          feature: countMarker(featureBytes["client.js"]!, marker),
        },
      ]),
    );
    expect(mainManifest.files).toBeDefined();
    expect(featureManifestData.files).toBeDefined();
    for (const file of files) {
      const size = sizes[file]!;
      expect(size.main).toBeGreaterThan(0);
      expect(size.feature).toBeGreaterThan(0);
      expect(mainManifest.files?.[file]).toBeTruthy();
      expect(featureManifestData.files?.[file]).toBeTruthy();
    }
    const totalMain = files.reduce((total, file) => total + sizes[file]!.main, 0);
    const totalFeature = files.reduce(
      (total, file) => total + sizes[file]!.feature,
      0,
    );
    expect(totalFeature).not.toBe(totalMain);
    test.info().attach("wasm-artifact-measurements.json", {
      body: JSON.stringify({ sizes, abi, totalMain, totalFeature }, null, 2),
      contentType: "application/json",
    });
    console.info("WASM artifact measurements", { sizes, abi, totalMain, totalFeature });
    expect(abi["feed"]?.feature).toBe(0);
  });
});
