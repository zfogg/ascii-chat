import { expect, test } from "@playwright/test";
import {
  getRandomPort,
  NativeClientFixture,
  ServerFixture,
} from "./server-fixture";

test.use({ viewport: { width: 1920, height: 1080 } });

test("/client?test2 sustains near-60-FPS video delivery and rendering for 15s at 1920x1080", async ({
  page,
}) => {
  test.setTimeout(60_000);
  const testStartedAt = Date.now();
  const server = new ServerFixture(getRandomPort());
  const peer = new NativeClientFixture(server.getPort(), 60);
  try {
    await server.start();
    await peer.start();

    await page.goto(
      `/client?test2&testServerUrl=${encodeURIComponent(server.getUrl())}`,
    );
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 20_000,
    });
    await expect
      .poll(
        () => page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
        { timeout: 10_000 },
      )
      .toBeGreaterThan(60);

    const samples: Array<{
      repaintFps: number;
      receivedFps: number;
      changingAsciiFps: number;
    }> = [];
    let previous = await page.evaluate(() => ({
      sampledAt: performance.now(),
      rendered: window.__clientFrameMetrics?.rendered ?? 0,
      received: window.__clientFrameMetrics?.received ?? 0,
      changedReceived: window.__clientFrameMetrics?.changedReceived ?? 0,
    }));

    for (let second = 0; second < 15; second++) {
      await page.waitForTimeout(1_000);
      await expect(page.locator(".status")).toContainText("Connected");
      const current = await page.evaluate(() => ({
        sampledAt: performance.now(),
        rendered: window.__clientFrameMetrics?.rendered ?? 0,
        received: window.__clientFrameMetrics?.received ?? 0,
        changedReceived: window.__clientFrameMetrics?.changedReceived ?? 0,
      }));
      const elapsedSeconds = (current.sampledAt - previous.sampledAt) / 1_000;
      samples.push({
        repaintFps: (current.rendered - previous.rendered) / elapsedSeconds,
        receivedFps: (current.received - previous.received) / elapsedSeconds,
        changingAsciiFps:
          (current.changedReceived - previous.changedReceived) / elapsedSeconds,
      });
      previous = current;
    }

    const rates = (key: "repaintFps" | "receivedFps" | "changingAsciiFps") =>
      samples.map((sample) => sample[key]);
    const average = (values: number[]) =>
      values.reduce((sum, value) => sum + value, 0) / values.length;
    const summary = {
      windows: samples.length,
      durationSeconds: Math.round((Date.now() - testStartedAt) / 1_000),
      repaintFps: {
        avg: average(rates("repaintFps")),
        min: Math.min(...rates("repaintFps")),
        windowsAtLeast55: rates("repaintFps").filter((fps) => fps >= 55).length,
      },
      receivedFps: {
        avg: average(rates("receivedFps")),
        min: Math.min(...rates("receivedFps")),
        windowsAtLeast55: rates("receivedFps").filter((fps) => fps >= 55)
          .length,
      },
      changingAsciiFps: {
        avg: average(rates("changingAsciiFps")),
        min: Math.min(...rates("changingAsciiFps")),
      },
    };
    console.log("CLIENT_TEST2_60FPS_15S", JSON.stringify(summary));
    expect(summary.repaintFps.windowsAtLeast55).toBeGreaterThanOrEqual(12);
    expect(summary.receivedFps.windowsAtLeast55).toBeGreaterThanOrEqual(12);
    expect(summary.changingAsciiFps.avg).toBeGreaterThanOrEqual(55);
    expect(
      rates("changingAsciiFps").filter((fps) => fps >= 50).length,
    ).toBeGreaterThanOrEqual(12);
  } finally {
    await peer.stop();
    await server.stop();
  }
});
