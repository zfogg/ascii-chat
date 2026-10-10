import { spawn, type ChildProcess } from "node:child_process";
import { mkdtempSync, readFileSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { test, expect, type Page } from "@playwright/test";
import { getRandomPort, waitForPort } from "./server-fixture";

class NativeProcess {
  child: ChildProcess;
  output = "";
  constructor(
    args: string[],
    readonly logPath: string,
    appData: string,
  ) {
    const binary =
      process.env["ASCII_CHAT_TEST_BINARY"] ||
      path.resolve("../../build/bin/ascii-chat");
    this.child = spawn(
      binary,
      ["--log-level", "debug", "--log-file", logPath, ...args],
      {
        windowsHide: true,
        env: { ...process.env, APPDATA: appData },
        stdio: ["ignore", "pipe", "pipe"],
      },
    );
    this.child.stdout?.on("data", (chunk) => {
      this.output += String(chunk);
    });
    this.child.stderr?.on("data", (chunk) => {
      this.output += String(chunk);
    });
    this.child.on("error", (error) => {
      this.output += String(error);
    });
  }
  logs() {
    try {
      return this.output + readFileSync(this.logPath, "utf8");
    } catch {
      return this.output;
    }
  }
  async stop() {
    if (this.child.exitCode !== null) return;
    await new Promise<void>((resolve) => {
      const timeout = setTimeout(() => {
        this.child.kill("SIGKILL");
        resolve();
      }, 5000);
      this.child.once("exit", () => {
        clearTimeout(timeout);
        resolve();
      });
      this.child.kill();
    });
  }
}

let server: NativeProcess;
let acds: NativeProcess;
let serverPort: number;
let acdsPort: number;
let session: string;
let root: string;
const password = "hevc-browser-test";
const host = process.env["ASCII_CHAT_TEST_HOST"] || "127.0.0.1";

test.beforeAll(async () => {
  root = mkdtempSync(path.join(tmpdir(), "ascii-hevc-"));
  const appData = path.join(root, "appdata");
  mkdirSync(appData);
  serverPort = getRandomPort();
  acdsPort = getRandomPort();
  acds = new NativeProcess(
    [
      "discovery-service",
      "--port",
      String(acdsPort),
      "--websocket-port",
      String(acdsPort + 1),
      "--database",
      path.join(root, "acds.db"),
      "--status-screen=false",
    ],
    path.join(root, "acds.log"),
    appData,
  );
  await waitForPort(acdsPort, "127.0.0.1");
  server = new NativeProcess(
    [
      "server",
      host,
      "--port",
      String(serverPort),
      "--websocket-port",
      String(serverPort + 1),
      "--discovery",
      "--discovery-service",
      "127.0.0.1",
      "--discovery-service-port",
      String(acdsPort),
      "--webrtc",
      "--password",
      password,
      "--stun-servers",
      "",
      "--turn-servers",
      "",
      "--status-screen=false",
    ],
    path.join(root, "server.log"),
    appData,
  );
  await waitForPort(serverPort + 1, host);
  await expect
    .poll(
      () =>
        server.logs().match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/i)?.[1],
      { timeout: 30_000 },
    )
    .toBeTruthy();
  session = server
    .logs()
    .match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/i)![1]!;
});
test.afterAll(async () => {
  await server?.stop();
  await acds?.stop();
});

async function connect(page: Page, mode: string, encoding: string) {
  const query = new URLSearchParams({ fps: "30" });
  if (encoding !== "default") query.set("encoding", encoding);
  if (mode === "client")
    query.set("serverUrl", `ws://${host}:${serverPort + 1}`);
  else {
    query.set("session", session);
    query.set("signalingUrl", `ws://127.0.0.1:${acdsPort + 1}`);
    query.set("stunUrls", "");
    query.set("turnUrls", "");
  }
  await page.addInitScript(() => {
    const original = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    navigator.mediaDevices.getUserMedia = async (constraints) => {
      if (!constraints?.video) return original(constraints);
      const canvas = document.createElement("canvas");
      canvas.width = 640;
      canvas.height = 480;
      const context = canvas.getContext("2d")!;
      let n = 0;
      const draw = () => {
        context.fillStyle = "#102040";
        context.fillRect(0, 0, 640, 480);
        for (let i = 0; i < 12; i++) {
          context.fillStyle = `hsl(${(n * 3 + i * 30) % 360} 90% 60%)`;
          context.fillRect((n * 7 + i * 51) % 640, i * 40, 100, 30);
        }
        n++;
        requestAnimationFrame(draw);
      };
      draw();
      const stream = canvas.captureStream(30);
      if (constraints.audio) {
        const audio = new AudioContext();
        const destination = audio.createMediaStreamDestination();
        const oscillator = audio.createOscillator();
        const gain = audio.createGain();
        gain.gain.value = 0;
        oscillator.connect(gain).connect(destination);
        oscillator.start();
        for (const track of destination.stream.getAudioTracks())
          stream.addTrack(track);
      }
      return stream;
    };
  });
  await page.goto(`/${mode}?${query}`);
  // Instrument the real transport before connecting; media and codec remain real.
  await page.evaluate(async () => {
    const probe = { raw: 0, hevc: 0, h264: 0, bytes: 0, keys: 0 };
    Object.assign(window, { __encodingProbe: probe });
    for (const name of ["ClientConnection", "WebRTCSession"]) {
      const module = await import(/* @vite-ignore */ `/src/network/${name}.ts`);
      const prototype = module[name].prototype;
      const send = prototype.sendPacket;
      prototype.sendPacket = function (type: number, payload: Uint8Array) {
        const result = send.call(this, type, payload);
        if (type === 3001) probe.raw++;
        if (type === 3002 || type === 3003) {
          if (type === 3002) probe.hevc++;
          else probe.h264++;
          if (payload[0]! & 1) probe.keys++;
        }
        if (type === 3001 || type === 3002 || type === 3003)
          probe.bytes += payload.length;
        return result;
      };
    }
  });
  if (mode === "discovery") {
    await page.getByText("Connection settings", { exact: true }).click();
    await page.getByLabel("Session password", { exact: true }).fill(password);
    await page
      .getByRole("button", { name: "Join session", exact: true })
      .click();
  } else {
    await page.getByLabel("Crypto password", { exact: true }).fill(password);
    await page.getByRole("button", { name: "Connect", exact: true }).click();
  }
  await expect(
    page.getByRole("button", { name: "Disconnect", exact: true }),
  ).toBeVisible({ timeout: 40_000 });
}
async function probe(page: Page) {
  return page.evaluate(() => ({
    ...(
      window as unknown as {
        __encodingProbe: {
          raw: number;
          hevc: number;
          h264: number;
          bytes: number;
          keys: number;
        };
      }
    ).__encodingProbe,
    rendered: window.__clientFrameMetrics?.rendered,
    changedReceived: window.__clientFrameMetrics?.changedReceived,
    uniqueRendered: window.__clientFrameMetrics?.uniqueRendered,
  }));
}
async function pixels(page: Page) {
  return page.locator("canvas.ascii-canvas").evaluate((source) => {
    const canvas = document.createElement("canvas");
    canvas.width = (source as HTMLCanvasElement).width;
    canvas.height = (source as HTMLCanvasElement).height;
    const ctx = canvas.getContext("2d")!;
    ctx.drawImage(source as HTMLCanvasElement, 0, 0);
    const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    let lit = 0;
    let hash = 2166136261;
    for (let i = 0; i < data.length; i += 4) {
      if (Math.max(data[i]!, data[i + 1]!, data[i + 2]!) > 40) lit++;
      hash = Math.imul(hash ^ data[i]!, 16777619);
      hash = Math.imul(hash ^ data[i + 1]!, 16777619);
      hash = Math.imul(hash ^ data[i + 2]!, 16777619);
    }
    return {
      lit,
      hash: hash >>> 0,
      width: canvas.width,
      height: canvas.height,
    };
  });
}
for (const mode of ["client", "discovery"]) {
  for (const encoding of ["raw", "hvec", "H.264"]) {
    test(`${mode}: ${encoding} uploads decode into changing ASCII frames`, async ({
      page,
    }, info) => {
      const errors: string[] = [];
      page.on("pageerror", (error) => errors.push(error.message));
      await connect(page, mode, encoding);
      await expect
        .poll(async () => (await probe(page)).changedReceived ?? 0, {
          timeout: 20_000,
        })
        .toBeGreaterThan(15);
      await expect
        .poll(async () => (await pixels(page)).lit)
        .toBeGreaterThan(100);
      const firstPixels = await pixels(page);
      const start = await probe(page);
      await page.waitForTimeout(5000);
      const end = await probe(page);
      const lastPixels = await pixels(page);
      expect(lastPixels.lit).toBeGreaterThan(100);
      expect(lastPixels.hash).not.toBe(firstPixels.hash);
      expect(
        (end.changedReceived ?? 0) - (start.changedReceived ?? 0),
      ).toBeGreaterThan(30);
      expect(end.rendered).toBeGreaterThan(start.rendered ?? 0);
      if (encoding === "raw") {
        expect(end.raw).toBeGreaterThan(30);
        expect(end.hevc).toBe(0);
        expect(end.h264).toBe(0);
      } else {
        expect(encoding === "H.264" ? end.h264 : end.hevc).toBeGreaterThan(30);
        expect(encoding === "H.264" ? end.hevc : end.h264).toBe(0);
        expect(end.raw).toBe(0);
        expect(end.keys).toBeGreaterThan(0);
      }
      await expect(page.getByRole("alert")).toHaveCount(0);
      const evidence = {
        mode,
        encoding,
        start,
        end,
        firstPixels,
        lastPixels,
        uploadBytesPerSecond: (end.bytes - start.bytes) / 5,
        browser: await page.evaluate(() => navigator.userAgent),
      };
      console.log(JSON.stringify(evidence));
      writeFileSync(
        info.outputPath("evidence.json"),
        JSON.stringify(evidence, null, 2),
      );
      await page.screenshot({
        path: info.outputPath("rendered.png"),
        fullPage: true,
      });
      // Restart capture and verify that HEVC recovers with a fresh keyframe.
      await page
        .getByRole("button", {
          name: mode === "discovery" ? "Disable webcam" : "Stop",
          exact: true,
        })
        .click();
      await page
        .getByRole("button", {
          name: mode === "discovery" ? "Enable webcam" : "Start Webcam",
          exact: true,
        })
        .click();
      await expect
        .poll(async () => (await probe(page)).changedReceived ?? 0)
        .toBeGreaterThan((end.changedReceived ?? 0) + 15);
      await expect
        .poll(async () => (await pixels(page)).hash)
        .not.toBe(lastPixels.hash);
      if (encoding !== "raw") {
        expect((await probe(page)).keys).toBeGreaterThan(end.keys);
        expect((await probe(page)).raw).toBe(0);
      }
      const beforeResize = (await probe(page)).rendered ?? 0;
      await page.setViewportSize({ width: 1100, height: 1000 });
      await expect
        .poll(async () => (await probe(page)).rendered ?? 0)
        .toBeGreaterThan(beforeResize + 15);
      await expect
        .poll(async () => (await pixels(page)).lit)
        .toBeGreaterThan(100);
      expect(errors).toEqual([]);
      await page
        .getByRole("button", { name: "Disconnect", exact: true })
        .click();
    });
  }
  test(`${mode}: unsupported forced HEVC errors without sending raw`, async ({
    page,
  }, info) => {
    await page.addInitScript(() => {
      Object.defineProperty(window, "VideoEncoder", {
        value: undefined,
        configurable: true,
      });
    });
    await connect(page, mode, "hevc");
    await expect(page.getByRole("alert")).toContainText(
      "HEVC encoding is unsupported",
      { timeout: 20_000 },
    );
    await expect(page.getByRole("alert")).toContainText("?encoding=raw");
    await page.waitForTimeout(1500);
    expect((await probe(page)).raw).toBe(0);
    expect((await probe(page)).hevc).toBe(0);
    await page.screenshot({
      path: info.outputPath("unsupported.png"),
      fullPage: true,
    });
  });
  for (const encoding of ["raw", "auto"]) {
    test(`${mode}: ${encoding} renders without WebCodecs`, async ({ page }) => {
      await page.addInitScript(() => {
        Object.defineProperty(window, "VideoEncoder", {
          value: undefined,
          configurable: true,
        });
      });
      await connect(page, mode, encoding);
      await expect
        .poll(async () => (await probe(page)).changedReceived ?? 0)
        .toBeGreaterThan(15);
      await expect
        .poll(async () => (await pixels(page)).lit)
        .toBeGreaterThan(100);
      expect((await probe(page)).raw).toBeGreaterThan(15);
      expect((await probe(page)).hevc).toBe(0);
      await expect(page.getByRole("alert")).toHaveCount(0);
    });
  }
  test(`${mode}: unsupported HEVC configuration errors clearly`, async ({
    page,
  }) => {
    await page.addInitScript(() => {
      VideoEncoder.isConfigSupported = async (config) => ({
        supported: false,
        config,
      });
    });
    await connect(page, mode, "hevc");
    await expect(page.getByRole("alert")).toContainText(
      "HEVC encoding is unsupported",
    );
    await expect(page.getByRole("alert")).toContainText("?encoding=raw");
    expect((await probe(page)).raw).toBe(0);
    expect((await probe(page)).hevc).toBe(0);
  });
  test(`${mode}: runtime HEVC failure stops capture without raw fallback`, async ({
    page,
  }) => {
    await connect(page, mode, "hevc");
    await expect.poll(async () => (await probe(page)).hevc).toBeGreaterThan(15);
    await page.evaluate(() => {
      VideoEncoder.prototype.encode = () => {
        throw new Error("Injected encoder failure");
      };
    });
    await expect(page.getByRole("alert")).toContainText("HEVC encoding failed");
    await expect(page.getByRole("alert")).toContainText("?encoding=raw");
    const stopped = await probe(page);
    await page.waitForTimeout(1000);
    expect((await probe(page)).hevc).toBe(stopped.hevc);
    expect((await probe(page)).raw).toBe(0);
  });
  for (const scenario of ["hevc", "h264", "raw"]) {
    test(`${mode}: default preference chooses ${scenario}`, async ({
      page,
    }) => {
      await page.addInitScript((selected) => {
        const probe = VideoEncoder.isConfigSupported.bind(VideoEncoder);
        VideoEncoder.isConfigSupported = async (config) => {
          if (
            selected === "raw" ||
            (selected === "h264" && config.codec.startsWith("hev"))
          )
            return { supported: false, config };
          return probe(config);
        };
      }, scenario);
      await connect(page, mode, "default");
      await expect
        .poll(
          async () => (await probe(page))[scenario as "hevc" | "h264" | "raw"],
        )
        .toBeGreaterThan(15);
      await expect
        .poll(async () => (await pixels(page)).lit)
        .toBeGreaterThan(100);
      const first = await pixels(page);
      await expect
        .poll(async () => (await pixels(page)).hash)
        .not.toBe(first.hash);
      await page
        .getByRole("button", { name: "Device setup", exact: true })
        .click();
      const selector = page.getByLabel("Video encoding", { exact: true });
      await expect(selector).toBeEnabled();
      await expect(selector).toHaveValue(scenario);
      const values = await selector
        .locator("option")
        .evaluateAll((options) =>
          options.map((option) => (option as HTMLOptionElement).value),
        );
      expect(values).toEqual(
        scenario === "hevc"
          ? ["auto", "hevc", "h264", "raw"]
          : scenario === "h264"
            ? ["auto", "h264", "raw"]
            : ["auto", "raw"],
      );
      await page.getByRole("button", { name: "Close device setup" }).click();
      for (const other of ["hevc", "h264", "raw"] as const)
        if (other !== scenario) expect((await probe(page))[other]).toBe(0);
      await expect(page.getByRole("alert")).toHaveCount(0);
    });
  }
  test(`${mode}: forced H.264 errors when unsupported`, async ({ page }) => {
    await page.addInitScript(() => {
      VideoEncoder.isConfigSupported = async (config) => ({
        supported: false,
        config,
      });
    });
    await connect(page, mode, "H.264");
    await expect(page.getByRole("alert")).toContainText(
      "H.264 encoding is unsupported",
    );
    await expect(page.getByRole("alert")).toContainText("?encoding=raw");
    expect((await probe(page)).raw).toBe(0);
    expect((await probe(page)).h264).toBe(0);
  });
  test(`${mode}: automatic runtime fallback renders HEVC then H.264 then raw`, async ({
    page,
  }) => {
    await page.addInitScript(() => {
      const codecs = new WeakMap<VideoEncoder, string>();
      const configure = VideoEncoder.prototype.configure;
      const encode = VideoEncoder.prototype.encode;
      Object.assign(window, { __failedCodecs: [] as string[] });
      VideoEncoder.prototype.configure = function (config) {
        codecs.set(this, config.codec);
        configure.call(this, config);
      };
      VideoEncoder.prototype.encode = function (frame, options) {
        const failed = (window as unknown as { __failedCodecs: string[] })
          .__failedCodecs;
        if (failed.some((prefix) => codecs.get(this)?.startsWith(prefix)))
          throw new Error("Injected codec failure");
        encode.call(this, frame, options);
      };
    });
    await connect(page, mode, "default");
    await expect.poll(async () => (await probe(page)).hevc).toBeGreaterThan(15);
    await page.evaluate(() =>
      Object.assign(window, { __failedCodecs: ["hev"] }),
    );
    await expect.poll(async () => (await probe(page)).h264).toBeGreaterThan(15);
    const first = await pixels(page);
    await page.evaluate(() =>
      Object.assign(window, { __failedCodecs: ["hev", "avc"] }),
    );
    await expect.poll(async () => (await probe(page)).raw).toBeGreaterThan(15);
    await expect
      .poll(async () => (await pixels(page)).hash)
      .not.toBe(first.hash);
    await expect
      .poll(async () => (await pixels(page)).lit)
      .toBeGreaterThan(100);
    await expect(page.getByRole("alert")).toHaveCount(0);
  });
  test(`${mode}: modal persists encoding while query overrides remain temporary`, async ({
    page,
  }, info) => {
    await connect(page, mode, "default");
    await expect.poll(async () => (await probe(page)).hevc).toBeGreaterThan(15);
    const storage = () =>
      page.evaluate(() => localStorage.getItem("ascii-chat.video-encoding"));
    await page
      .getByRole("button", { name: "Device setup", exact: true })
      .click();
    const selector = page.getByLabel("Video encoding", { exact: true });
    await expect(selector).toBeEnabled();
    await expect(selector).toHaveValue("hevc");
    expect(await storage()).toBeNull();
    await selector.selectOption("h264");
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    expect(await storage()).toBeNull();
    await page
      .getByRole("button", { name: "Device setup", exact: true })
      .click();
    await selector.selectOption("h264");
    await page.screenshot({
      path: info.outputPath("encoding-selector.png"),
      fullPage: true,
    });
    await page.getByRole("button", { name: "Done", exact: true }).click();
    expect(await storage()).toBe("h264");
    expect(new URL(page.url()).searchParams.get("encoding")).toBe("H.264");
    await expect.poll(async () => (await probe(page)).h264).toBeGreaterThan(15);
    await expect
      .poll(async () => (await pixels(page)).lit)
      .toBeGreaterThan(100);
    const temporary = new URL(page.url());
    temporary.searchParams.set("encoding", "raw");
    await page.goto(temporary.toString());
    await page
      .getByRole("button", { name: "Device setup", exact: true })
      .click();
    await expect(selector).toBeEnabled();
    await expect(selector).toHaveValue("raw");
    await page.getByRole("button", { name: "Done", exact: true }).click();
    expect(await storage()).toBe("h264");
    temporary.searchParams.delete("encoding");
    await page.goto(temporary.toString());
    await page
      .getByRole("button", { name: "Device setup", exact: true })
      .click();
    await expect(selector).toBeEnabled();
    await expect(selector).toHaveValue("h264");
    expect(await storage()).toBe("h264");
  });
}
