import { expect, test } from "@playwright/test";
import { spawn, type ChildProcess } from "child_process";
import * as fs from "fs";
import * as os from "os";
import * as path from "path";
import { getRandomPort, waitForPort } from "./server-fixture";

test.use({ viewport: { width: 1920, height: 1080 } });

async function stopProcess(process: ChildProcess): Promise<void> {
  if (process.exitCode !== null) return;
  await new Promise<void>((resolve) => {
    const timeout = setTimeout(() => {
      process.kill("SIGKILL");
      resolve();
    }, 5_000);
    process.once("exit", () => {
      clearTimeout(timeout);
      resolve();
    });
    process.kill("SIGTERM");
  });
}

test("/client?test2 sustains near-60-FPS video delivery and rendering for 15s at 1920x1080", async ({
  page,
}) => {
  test.setTimeout(60_000);
  const testStartedAt = Date.now();
  const port = getRandomPort();
  const configuredBinary = process.env["ASCII_CHAT_TEST_BINARY"];
  const binary = configuredBinary
    ? path.resolve(configuredBinary)
    : path.resolve(process.cwd(), "../../build/bin/ascii-chat");
  const serverLog = `C:\\tmp\\ascii-chat-client-test2-${port}.log`;
  const server = spawn(
    binary,
    [
      "--no-check-update",
      "--log-level",
      "info",
      "--log-file",
      serverLog,
      "server",
      "--port",
      String(port),
      "--websocket-port",
      String(port + 1),
    ],
    { stdio: "ignore" },
  );
  const appData = fs.mkdtempSync(
    path.join(os.tmpdir(), "ascii-client-test2-60fps-"),
  );
  let peer: ChildProcess | null = null;

  try {
    await Promise.all([
      waitForPort(port, "127.0.0.1"),
      waitForPort(port + 1, "127.0.0.1"),
    ]);
    peer = spawn(
      binary,
      [
        "--no-check-update",
        "client",
        `127.0.0.1:${port}`,
        "--test-pattern",
        "--fps",
        "60",
      ],
      { env: { ...process.env, APPDATA: appData }, stdio: "ignore" },
    );
    await new Promise((resolve) => setTimeout(resolve, 1_000));

    await page.goto(
      `/client?test2&testServerUrl=${encodeURIComponent(`ws://127.0.0.1:${port + 1}`)}`,
    );
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 20_000,
    });
    await expect
      .poll(() =>
        page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
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
      if (server.exitCode !== null) {
        throw new Error(`ascii-chat server exited with code ${server.exitCode}`);
      }
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
        windowsAtLeast55: rates("receivedFps").filter((fps) => fps >= 55).length,
      },
      changingAsciiFps: {
        avg: average(rates("changingAsciiFps")),
        min: Math.min(...rates("changingAsciiFps")),
      },
    };
    console.log("CLIENT_TEST2_60FPS_15S", JSON.stringify(summary));
    console.log(
      "CLIENT_TEST2_SERVER_RATES",
      fs.existsSync(serverLog)
        ? fs
            .readFileSync(serverLog, "utf8")
            .split(/\r?\n/)
            .filter((line) => /Server video render|Server ASCII delivery|Server image ingress|SEND_THREAD:/.test(line))
            .slice(-20)
            .join("\n")
        : `server log was not created (exitCode=${server.exitCode}, signalCode=${server.signalCode})`,
    );
    expect(summary.repaintFps.windowsAtLeast55).toBeGreaterThanOrEqual(12);
    expect(summary.receivedFps.windowsAtLeast55).toBeGreaterThanOrEqual(12);
    expect(summary.changingAsciiFps.avg).toBeGreaterThan(0);
  } finally {
    if (peer) await stopProcess(peer);
    await stopProcess(server);
    fs.rmSync(appData, { recursive: true, force: true });
  }
});
