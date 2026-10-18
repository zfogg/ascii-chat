import { expect, test } from "@playwright/test";
import { ServerFixture, getRandomPort } from "./server-fixture";

const origin =
  process.env["PARTICIPANT_TEST_ORIGIN"] || "http://localhost:33198";
let server: ServerFixture;
test.afterAll(async () => {
  await server?.stop();
});

test("shared C lifecycle and browser media survive stop and restart", async ({
  page,
}) => {
  test.setTimeout(90000);
  server = new ServerFixture(getRandomPort());
  await server.start();
  await page.goto(
    `${origin}/client?test&testServerUrl=${encodeURIComponent(server.getUrl())}`,
  );
  await expect(page.locator(".status")).toContainText("Connected", {
    timeout: 30000,
  });
  await expect
    .poll(
      () => page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
      { timeout: 20000 },
    )
    .toBeGreaterThan(15);
  await page
    .getByRole("button", { name: "Enable microphone", exact: true })
    .click();
  await expect(page.getByTestId("audio-levels")).toHaveAttribute(
    "data-sent",
    /[1-9]\d*/,
    { timeout: 10000 },
  );
  await page.screenshot({
    path: "../../build/evidence/participant-streaming.png",
    fullPage: true,
  });

  const states = await page.evaluate(async () => {
    const path = "/src/wasm/MediaLifecycle.ts";
    const { MediaLifecycle } = await import(path);
    const lifecycle = new MediaLifecycle();
    const first = lifecycle.begin();
    const before = lifecycle.active;
    const duplicate = lifecycle.begin();
    lifecycle.stop(() => {});
    const cancelled = lifecycle.complete(first);
    const second = lifecycle.begin();
    const stale = lifecycle.complete(first);
    const started = lifecycle.complete(second);
    const active = lifecycle.active;
    lifecycle.stop(() => {});
    const stopped = lifecycle.active;
    lifecycle.dispose();
    return { before, duplicate, cancelled, stale, started, active, stopped };
  });
  expect(states).toEqual({
    before: false,
    duplicate: 0,
    cancelled: false,
    stale: false,
    started: true,
    active: true,
    stopped: false,
  });

  for (let cycle = 0; cycle < 2; cycle++) {
    await page.getByRole("button", { name: "Stop", exact: true }).click();
    await expect(
      page.getByRole("button", { name: "Start Webcam", exact: true }),
    ).toBeVisible();
    await page
      .getByRole("button", { name: "Start Webcam", exact: true })
      .click();
    await expect(
      page.getByRole("button", { name: "Stop", exact: true }),
    ).toBeVisible();
  }

  for (let cycle = 0; cycle < 3; cycle++) {
    await page
      .getByRole("button", { name: "Mute microphone", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Enable microphone", exact: true })
      .click();
    await expect(page.getByTestId("audio-levels")).toHaveAttribute(
      "data-sent",
      /[1-9]\d*/,
    );
  }
  await page
    .getByRole("button", { name: "Disconnect", exact: true })
    .first()
    .click();
  await expect(page.locator(".status")).toContainText("Disconnected");
  await page.screenshot({
    path: "../../build/evidence/participant-disconnected.png",
    fullPage: true,
  });
});

test("real WASM rejects late microphone startup after cancellation", async ({
  page,
}) => {
  await page.goto(`${origin}/client`);
  const result = await page.evaluate(async () => {
    const clientPath = "/src/wasm/client.ts";
    const audioPath = "/src/audio/AudioPipeline.ts";
    const { ensureWasmModuleLoaded } = await import(clientPath);
    const { AudioPipeline } = await import(audioPath);
    await ensureWasmModuleLoaded();
    const context = new AudioContext({ sampleRate: 48000 });
    const output = context.createMediaStreamDestination();
    const stream = output.stream;
    let grant!: (stream: MediaStream) => void;
    let requested!: () => void;
    const request = new Promise<void>((resolve) => {
      requested = resolve;
    });
    const original = navigator.mediaDevices.getUserMedia.bind(
      navigator.mediaDevices,
    );
    navigator.mediaDevices.getUserMedia = () =>
      new Promise<MediaStream>((resolve) => {
        grant = resolve;
        requested();
      });
    const pipeline = new AudioPipeline();
    try {
      const pending = pipeline.startCapture();
      await request;
      pipeline.stopCapture();
      grant(stream);
      await pending;
      return {
        active: pipeline.captureActive,
        tracks: stream.getTracks().map((track) => track.readyState),
      };
    } finally {
      pipeline.close();
      navigator.mediaDevices.getUserMedia = original;
      await context.close();
    }
  });
  expect(result).toEqual({ active: false, tracks: ["ended"] });
});
