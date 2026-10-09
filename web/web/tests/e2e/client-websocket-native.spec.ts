import { expect, test } from "@playwright/test";
import {
  getRandomPort,
  NativeClientFixture,
  ServerFixture,
  expectMeaningful60Fps,
} from "./server-fixture";

test("native CLI producer and browser WebSocket client render near 60 FPS", async ({
  page,
  context,
}) => {
  test.setTimeout(45_000);
  const server = new ServerFixture(getRandomPort());
  const cli = new NativeClientFixture(server.getPort(), 60);

  await server.start();
  await cli.start();

  try {
    await context.grantPermissions(["camera", "microphone"]);
    await page.addInitScript(() => {
      const canvas = document.createElement("canvas");
      canvas.width = 320;
      canvas.height = 240;
      const context = canvas.getContext("2d");
      if (!context) throw new Error("Could not create test-video canvas");
      let frame = 0;
      const draw = () => {
        context.fillStyle = `hsl(${frame++ % 360} 80% 50%)`;
        context.fillRect(0, 0, canvas.width, canvas.height);
        requestAnimationFrame(draw);
      };
      draw();
      const stream = canvas.captureStream(60);
      const original = navigator.mediaDevices.getUserMedia.bind(
        navigator.mediaDevices,
      );
      navigator.mediaDevices.getUserMedia = (constraints) =>
        constraints?.video
          ? Promise.resolve(stream)
          : original(constraints ?? {});
    });

    await page.goto(
      `http://localhost:3000/client?serverUrl=${encodeURIComponent(server.getUrl())}`,
      { waitUntil: "networkidle" },
    );
    await expect(page.getByText("Disconnected", { exact: true })).toBeVisible();
    await page.getByRole("button", { name: "Connect", exact: true }).click();
    await expect(page.locator(".status")).toHaveText("Connected", {
      timeout: 15_000,
    });

    await expect
      .poll(
        () => page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
        { timeout: 15_000 },
      )
      .toBeGreaterThan(10);

    const start = await page.evaluate(
      () => window.__clientFrameMetrics?.rendered ?? 0,
    );
    const startedAt = performance.now();
    await page.waitForTimeout(5_000);
    const end = await page.evaluate(
      () => window.__clientFrameMetrics?.rendered ?? 0,
    );
    const fps = (end - start) / ((performance.now() - startedAt) / 1_000);

    console.log(
      `[client-websocket-native] rendered ${fps.toFixed(1)} FPS for 5s`,
    );
    expect(fps).toBeGreaterThanOrEqual(55);
    expect(fps).toBeLessThanOrEqual(60);
    await expect(page.getByText(/FPS: (5\d|60) \/ 60/)).toBeVisible();
    await expectMeaningful60Fps(page, "client", 5_000);
  } finally {
    await cli.stop();
    await server.stop();
  }
});
