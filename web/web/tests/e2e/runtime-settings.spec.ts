import { expect, test, type Page } from "@playwright/test";
import { spawn, type ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { createServer } from "node:net";

async function freePort() {
  const server = createServer();
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = (server.address() as { port: number }).port;
  await new Promise<void>((resolve) => server.close(() => resolve()));
  return port;
}

const frame = async (page: Page) => {
  await page.bringToFront();
  return page.evaluate(
    () => window.__clientFrameMetrics?.lastRenderedFrame ?? "",
  );
};
const colors = (page: Page) =>
  page.locator('select:has(option[value="truecolor"])');
const palettes = (page: Page) =>
  page.locator('select:has(option[value="standard"])');

const effective = (page: Page) =>
  page.evaluate(async () => {
    const url = "/src/wasm/client.ts";
    const { getClientModule } = await import(url);
    const module = getClientModule();
    if (!module) return {};
    const pointer = module._client_settings_values();
    try {
      return JSON.parse(module.UTF8ToString(pointer));
    } finally {
      module._wasm_options_release(pointer);
    }
  });

const receivedFps = (page: Page) =>
  page.evaluate(async () => {
    const before = window.__clientFrameMetrics?.received ?? 0;
    const start = performance.now();
    await new Promise((resolve) => setTimeout(resolve, 1200));
    return (
      (((window.__clientFrameMetrics?.received ?? 0) - before) * 1000) /
      (performance.now() - start)
    );
  });

test("native host applies live browser settings and isolates two viewers", async ({
  page,
  context,
}) => {
  const directory = mkdtempSync(join(tmpdir(), "ascii-runtime-browser-"));
  const tcp = await freePort(),
    ws = await freePort();
  const log = join(directory, "server.log");
  const binary =
    process.env["ASCII_CHAT_TEST_BINARY"] ??
    resolve(
      `../../build/bin/ascii-chat${process.platform === "win32" ? ".exe" : ""}`,
    );
  let server: ChildProcess | undefined;
  const second = await context.newPage();
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  try {
    server = spawn(
      binary,
      [
        "--no-check-update",
        "--log-level",
        "info",
        "--log-file",
        log,
        "server",
        "127.0.0.1",
        "--port",
        String(tcp),
        "--websocket-port",
        String(ws),
        "--status-screen=false",
      ],
      { cwd: directory, windowsHide: true, stdio: "ignore" },
    );
    await expect
      .poll(() => {
        try {
          return readFileSync(log, "utf8");
        } catch {
          return "";
        }
      })
      .toContain("Server accepting connections");
    const url = `/client?test&testServerUrl=${encodeURIComponent(`ws://127.0.0.1:${ws}`)}&settings=true&colorMode=truecolor`;
    await page.goto(url);
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });
    await expect(
      page
        .getByRole("status")
        .filter({ hasText: "Rendering settings applied" }),
    ).toBeVisible({ timeout: 15000 });
    await expect.poll(() => frame(page)).toContain("\x1b[38;2;");
    await page.getByLabel("Width", { exact: true }).fill("40");
    await page.getByLabel("Height", { exact: true }).fill("12");
    await expect.poll(async () => (await effective(page)).height).toBe(12);
    await second.goto(url);
    await expect(second.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });
    await expect.poll(() => frame(second)).toContain("\x1b[38;2;");
    await second.getByLabel("Width", { exact: true }).fill("40");
    await second.getByLabel("Height", { exact: true }).fill("12");
    await expect.poll(async () => (await effective(second)).height).toBe(12);
    for (const [mode, sequence] of [
      ["none", ""],
      ["16", "\x1b["],
      ["256", "\x1b[38;5;"],
      ["truecolor", "\x1b[38;2;"],
    ] as const) {
      await colors(page).selectOption(mode);
      await expect(colors(page)).toHaveValue(mode);
      await expect
        .poll(async () => (await effective(page)).color_mode)
        .toBe(["none", "16", "256", "truecolor"].indexOf(mode));
      await expect
        .poll(() => frame(page))
        .toMatch(
          mode === "none"
            ? /^(?!.*\x1b\[38;)/s
            : new RegExp(sequence.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")),
        );
      await expect.poll(() => frame(second)).toContain("\x1b[38;2;");
    }
    for (const palette of [
      "blocks",
      "digital",
      "minimal",
      "cool",
      "standard",
    ]) {
      await palettes(page).selectOption(palette);
      await expect(palettes(page)).toHaveValue(palette);
      await expect
        .poll(async () => (await effective(page)).palette_type)
        .toBe(
          ["standard", "blocks", "digital", "minimal", "cool"].indexOf(
            palette,
          ) + 1,
        );
      await expect(
        page
          .getByRole("status")
          .filter({ hasText: "Rendering settings applied" }),
      ).toBeVisible();
    }
    for (const [filter, value] of [
      ["green", 3],
      ["rainbow", 12],
      ["none", 0],
    ] as const) {
      await page
        .locator('select:has(option[value="rainbow"])')
        .selectOption(filter);
      await expect
        .poll(async () => (await effective(page)).color_filter)
        .toBe(value);
    }
    for (const mode of [1, 2, 0]) {
      await page
        .getByLabel("Render Mode", { exact: true })
        .selectOption(String(mode));
      await expect
        .poll(async () => (await effective(page)).render_mode)
        .toBe(mode);
      if (mode === 1)
        await expect.poll(() => frame(page)).toContain("\x1b[48;2;");
      if (mode === 2)
        await expect.poll(() => frame(page)).toMatch(/[\u2580\u2588]/u);
    }
    const fps = page.locator('input[type="range"]').first();
    await fps.press("Home");
    await expect.poll(async () => (await effective(page)).fps).toBe(15);
    const lowFps = await receivedFps(page);
    expect(lowFps).toBeGreaterThan(5);
    expect(lowFps).toBeLessThan(22);
    await fps.press("End");
    await expect.poll(async () => (await effective(page)).fps).toBe(60);
    await expect(fps).toHaveValue("60");
    const highFps = await receivedFps(page);
    expect(highFps).toBeGreaterThan(25);
    expect(highFps).toBeGreaterThan(lowFps * 1.5);
    const measuredRates = test.info().outputPath("received-frame-rates.json");
    writeFileSync(
      measuredRates,
      JSON.stringify({ requested15: lowFps, requested60: highFps }),
    );
    await test.info().attach("received-frame-rates.json", {
      path: measuredRates,
      contentType: "application/json",
    });
    await page.getByLabel("Width", { exact: true }).fill("55");
    await page.getByLabel("Height", { exact: true }).fill("13");
    await expect
      .poll(async () => (await frame(page)).split("\n").length)
      .toBe(13);
    await expect
      .poll(async () => (await frame(second)).split("\n").length)
      .toBe(12);
    await page.locator("canvas.ascii-canvas").focus();
    await page.keyboard.press("c");
    await expect(colors(page)).toHaveValue("none");
    await expect.poll(() => frame(page)).not.toContain("\x1b[38;2;");
    const canvas = page.locator("canvas.ascii-canvas");
    expect(
      await canvas.evaluate((c: HTMLCanvasElement) =>
        c
          .getContext("2d")!
          .getImageData(0, 0, c.width, c.height)
          .data.some((v, i) => i % 4 !== 3 && v > 16),
      ),
    ).toBe(true);
    await page.screenshot({
      path: "artifacts/runtime-settings-native-browser.png",
    });
    await page.getByRole("button", { name: "Disconnect", exact: true }).click();
    await page.getByRole("button", { name: "Connect", exact: true }).click();
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });
    await expect.poll(async () => (await effective(page)).color_mode).toBe(0);
    await expect.poll(async () => (await effective(page)).width).toBe(55);
    await page.reload();
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });
    await expect(colors(page)).toHaveValue("none");
    await expect(page.getByLabel("Width", { exact: true })).toHaveValue("0");
    await expect(page.getByLabel("Height", { exact: true })).toHaveValue("0");
    await expect.poll(() => frame(page)).not.toBe("");
    expect(errors).toEqual([]);
  } finally {
    try {
      await test.info().attach("native-host.log", {
        body: readFileSync(log),
        contentType: "text/plain",
      });
    } catch {
      /* The process may have failed before creating its log. */
    }
    await Promise.allSettled([
      page.goto("about:blank", { timeout: 5000 }),
      second.goto("about:blank", { timeout: 5000 }),
    ]);
    await second.close();
    if (server?.exitCode === null) {
      server.kill();
      await new Promise<void>((resolve) =>
        server!.once("exit", () => resolve()),
      );
    }
    rmSync(directory, { recursive: true, force: true });
  }
});

test("browser C host acknowledges atomic per-viewer updates and rejects invalid patches", async ({
  page,
}) => {
  await page.goto("/mirror");
  const result = await page.evaluate(async () => {
    const runtimeUrl = "/src/network/BrowserHostRuntime.ts";
    const buildersUrl = "/src/network/packetBuilders.ts";
    const { BrowserHostRuntime } = await import(runtimeUrl);
    const { buildCapabilitiesPacket } = await import(buildersUrl);
    const replies: {
      id: number;
      request: number;
      status: number;
      values: Record<string, unknown>;
    }[] = [];
    const errors: string[] = [];
    const runtime = new BrowserHostRuntime(
      (id: number, bytes: Uint8Array) => {
        const view = new DataView(
          bytes.buffer,
          bytes.byteOffset,
          bytes.byteLength,
        );
        if (view.getUint16(8) !== 6064) return;
        replies.push({
          id,
          request: view.getUint32(26),
          status: view.getUint32(38),
          values: JSON.parse(new TextDecoder().decode(bytes.subarray(42))),
        });
      },
      (error: Error) => errors.push(error.message),
    );
    await runtime.start();
    const first = runtime.addClient(),
      second = runtime.addClient();
    runtime.receive(first, 5000, buildCapabilitiesPacket(40, 12));
    runtime.receive(second, 5000, buildCapabilitiesPacket(80, 24));
    const wait = async (count: number) => {
      const end = performance.now() + 15000;
      while (
        replies.length < count &&
        !errors.length &&
        performance.now() < end
      )
        await new Promise((resolve) => setTimeout(resolve, 10));
      if (replies.length < count)
        throw new Error(JSON.stringify({ errors, replies }));
    };
    const send = (
      id: number,
      request: number,
      values?: Record<string, unknown>,
    ) => {
      const json = values
        ? new TextEncoder().encode(JSON.stringify(values))
        : new Uint8Array();
      const bytes = new Uint8Array(20 + json.length),
        view = new DataView(bytes.buffer);
      view.setUint32(0, 1);
      view.setUint32(4, request);
      view.setUint32(8, 0xffffffff);
      bytes.set(json, 20);
      runtime.receive(id, 6063, bytes);
    };
    try {
      await wait(2);
      send(first, 1, {
        width: 55,
        color_mode: 0,
        palette_type: 6,
        palette_custom: " .+#",
        render_mode: 2,
        matrix_rain: true,
        fps: 20,
      });
      await wait(3);
      send(first, 2, { width: 99, password: "must-not-apply" });
      await wait(4);
      send(second, 3, { color_filter: 3, fps: 120 });
      await wait(5);
      runtime.receive(first, 5000, buildCapabilitiesPacket(40, 12));
      send(first, 4);
      await wait(6);
      return { replies, errors };
    } finally {
      runtime.close();
    }
  });
  expect(result.errors).toEqual([]);
  const first = result.replies.find((r) => r.request === 1)!;
  const rejected = result.replies.find((r) => r.request === 2)!;
  const second = result.replies.find((r) => r.request === 3)!;
  const afterCodecs = result.replies.find((r) => r.request === 4)!;
  expect(first.status).toBe(0);
  expect(first.values).toMatchObject({
    width: 55,
    color_mode: 0,
    palette_type: 6,
    palette_custom: " .+#",
    render_mode: 2,
    matrix_rain: true,
    fps: 20,
  });
  expect(rejected.status).not.toBe(0);
  expect(rejected.values).toEqual(first.values);
  expect(afterCodecs.values).toEqual(first.values);
  expect(second.values).toMatchObject({
    width: 80,
    height: 24,
    color_mode: 3,
    color_filter: 3,
    fps: 60,
  });
});
