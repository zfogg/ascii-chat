import { expect, test, type Page } from "@playwright/test";
import { ServerFixture } from "./server-fixture";

// Supply real VideoFrames through getUserMedia. The built-in ?test source bypasses
// camera scheduling and cannot detect a camera callback gate that halves 60 FPS.
async function camera(page: Page) {
  await page.addInitScript(() => {
    const stats = { generated: 0, uploaded: 0, painted: 0, changed: 0 };
    Object.assign(window, { cameraCadence: stats });
    const send = WebSocket.prototype.send;
    WebSocket.prototype.send = function (data) {
      if (ArrayBuffer.isView(data) && data.byteLength > 1024) stats.uploaded++;
      else if (data instanceof ArrayBuffer && data.byteLength > 1024)
        stats.uploaded++;
      return send.call(this, data);
    };
    const paint = CanvasRenderingContext2D.prototype.putImageData;
    let previous = 0;
    CanvasRenderingContext2D.prototype.putImageData = function (
      image: ImageData,
      dx: number,
      dy: number,
      ...dirty: number[]
    ) {
      if (this.canvas.classList.contains("ascii-canvas")) {
        stats.painted++;
        let hash = 2166136261;
        const pixels = image.data;
        // Sample the content area, excluding the FPS label along the top row.
        for (
          let i = Math.floor(pixels.length / 4);
          i < pixels.length;
          i += 4001
        )
          hash = Math.imul(hash ^ pixels[i]!, 16777619);
        if (hash !== previous) stats.changed++;
        previous = hash;
      }
      Reflect.apply(paint, this, [image, dx, dy, ...dirty]);
    };
    navigator.mediaDevices.getUserMedia = async () => {
      const Generator = (
        window as unknown as {
          MediaStreamTrackGenerator: new (options: {
            kind: string;
          }) => MediaStreamTrack & {
            writable: WritableStream<VideoFrame>;
          };
        }
      ).MediaStreamTrackGenerator;
      const track = new Generator({ kind: "video" });
      const writer = track.writable.getWriter();
      const canvas = document.createElement("canvas");
      canvas.width = 320;
      canvas.height = 180;
      const context = canvas.getContext("2d")!;
      let frame = 0;
      const start = performance.now();
      const tick = async () => {
        if (track.readyState === "ended") return;
        context.fillStyle = `rgb(${120 + (frame % 120)},${120 + ((frame * 3) % 120)},${120 + ((frame * 7) % 120)})`;
        context.fillRect(0, 0, 320, 180);
        context.fillStyle = "white";
        context.fillRect((frame * 3) % 288, 30, 32, 120);
        const image = new VideoFrame(canvas, {
          timestamp: Math.round((frame * 1e6) / 60),
        });
        try {
          await writer.write(image);
          stats.generated++;
        } catch {
          return;
        } finally {
          image.close();
        }
        // A camera drops stale frames during a host stall instead of emitting
        // an unbounded catch-up burst afterwards.
        frame = Math.max(
          frame + 1,
          Math.floor(((performance.now() - start) * 60) / 1000),
        );
        const jitter = frame % 2 ? -0.7 : 0.7;
        setTimeout(
          tick,
          Math.max(0, start + (frame * 1000) / 60 + jitter - performance.now()),
        );
      };
      void tick();
      // No microphone or speaker tracks are created, including Mirror's audio request.
      return new MediaStream([track]);
    };
  });
}

async function measure(page: Page) {
  return page.evaluate(() => ({
    now: performance.now(),
    received:
      (window as unknown as { __clientFrameMetrics?: { received: number } })
        .__clientFrameMetrics?.received ?? 0,
    sourceChanged:
      (
        window as unknown as {
          __clientFrameMetrics?: { changedReceived: number };
        }
      ).__clientFrameMetrics?.changedReceived ?? 0,
    ...(
      window as unknown as {
        cameraCadence: {
          generated: number;
          uploaded: number;
          painted: number;
          changed: number;
        };
      }
    ).cameraCadence,
  }));
}

test.use({ viewport: { width: 800, height: 850 } });

test("Mirror presents fresh camera frames near 60 FPS", async ({ page }) => {
  await camera(page);
  await page.goto("/mirror?playing=true");
  await page.waitForFunction(
    () =>
      (window as unknown as { cameraCadence: { changed: number } })
        .cameraCadence.changed > 120,
  );
  await page.waitForTimeout(3000);
  const before = await measure(page);
  await page.waitForTimeout(10000);
  const after = await measure(page);
  console.log("Mirror camera cadence", before, after);
  expect(
    ((after.changed! - before.changed!) * 1000) / (after.now - before.now),
  ).toBeGreaterThanOrEqual(55);
});

test("two camera clients upload at 60 FPS and preserve fresh received content", async ({
  page,
  browser,
  baseURL,
}) => {
  test.setTimeout(60000);
  const server = new ServerFixture(38424, ["--status-screen=false"]);
  const second = await browser.newPage({
    ...(baseURL ? { baseURL } : {}),
    viewport: { width: 800, height: 850 },
  });
  const pages = [page, second];
  try {
    await server.start();
    await Promise.all(
      pages.map(async (client) => {
        await camera(client);
        await client.goto(
          "/client?serverUrl=ws%3A%2F%2F127.0.0.1%3A38425&connect=true&encoding=raw",
        );
        await client.waitForFunction(
          () =>
            (window as unknown as { cameraCadence: { uploaded: number } })
              .cameraCadence.uploaded > 120,
        );
      }),
    );
    await page.waitForTimeout(3000);
    const before = await Promise.all(pages.map(measure));
    await page.waitForTimeout(10000);
    const after = await Promise.all(pages.map(measure));
    console.log("Two-client camera cadence", before, after);
    for (let i = 0; i < pages.length; i++) {
      const elapsed = after[i]!.now - before[i]!.now;
      const rate = (counter: "uploaded" | "changed" | "sourceChanged") =>
        ((after[i]![counter] - before[i]![counter]) * 1000) / elapsed;
      expect(
        rate("uploaded"),
        `client ${i + 1} camera upload FPS`,
      ).toBeGreaterThanOrEqual(55);
      // The native server may coalesce camera snapshots. Check actual changing
      // output against fresh received content, rather than counting duplicate paints.
      expect(
        rate("sourceChanged"),
        `client ${i + 1} fresh server FPS`,
      ).toBeGreaterThanOrEqual(50);
      expect(
        rate("changed"),
        `client ${i + 1} fresh canvas FPS`,
      ).toBeGreaterThanOrEqual(rate("sourceChanged") * 0.9);
    }
  } finally {
    await second.close();
    await server.stop();
  }
});
