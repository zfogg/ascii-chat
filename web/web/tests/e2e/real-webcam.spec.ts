import { test, expect } from "@playwright/test";
import { ServerFixture, getRandomPort } from "./server-fixture";

let server: ServerFixture | null = null;
let serverUrl: string = "";

test.beforeAll(async () => {
  const port = getRandomPort();
  server = new ServerFixture(port);
  await server.start();
  serverUrl = server.getUrl();
  console.log(`✓ Server started on ${serverUrl}`);
});

test.afterAll(async () => {
  if (server) {
    await server.stop();
  }
});

test("Real webcam animates with server frames", async ({ page, context }) => {
  test.setTimeout(20000);

  // Grant real camera permission - this will use the ACTUAL system webcam
  await context.grantPermissions(["camera", "microphone"]);

  const frameChecksums: string[] = [];
  const receiveTimestamps: number[] = [];

  page.on("console", (msg) => {
    const text = msg.text();

    // Capture frame checksums to verify frames are different
    // The format is "hash=123456" (decimal number)
    if (text.includes("hash=")) {
      const match = text.match(/hash=(\d+)/);
      if (match) {
        frameChecksums.push(match[1]);
        receiveTimestamps.push(Date.now());
        console.log(`[FRAME #${frameChecksums.length}] checksum=${match[1]}`);
      }
    }
  });

  console.log("Opening browser client with REAL WEBCAM...");
  const clientUrl = `http://localhost:3000/client?testServerUrl=${encodeURIComponent(serverUrl)}`;
  await page.goto(clientUrl, { waitUntil: "networkidle" });

  console.log("\nWaiting 5 seconds for frames to render from real webcam...");
  await page.waitForTimeout(5000);

  console.log(`\n========== REAL WEBCAM TEST RESULTS ==========`);
  console.log(`Total frames received: ${frameChecksums.length}`);
  console.log(`Unique frame checksums: ${new Set(frameChecksums).size}`);
  console.log(
    `Checksums: ${frameChecksums.slice(0, 10).join(", ")}${frameChecksums.length > 10 ? "..." : ""}`,
  );

  // With real webcam input, frames MUST be different from each other
  const uniqueChecksums = new Set(frameChecksums);
  expect(frameChecksums.length).toBeGreaterThan(
    0,
    "No frames received from server",
  );
  expect(uniqueChecksums.size).toBeGreaterThan(
    1,
    `Got ${frameChecksums.length} frames but only ${uniqueChecksums.size} unique. Frames must change with real webcam input.`,
  );
});
