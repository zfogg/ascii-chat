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

test("Browser receives multiple ENCRYPTED packets from server", async ({
  page,
  context,
}) => {
  test.setTimeout(15000);

  const encryptedPackets: number[] = [];
  const asciiFrames: number[] = [];

  await context.grantPermissions(["camera", "microphone"]);

  // Mock webcam
  await page.addInitScript(() => {
    const canvas = document.createElement("canvas");
    canvas.width = 640;
    canvas.height = 480;
    const ctx = canvas.getContext("2d");
    const stream = canvas.captureStream(30);

    let frameCount = 0;
    setInterval(() => {
      ctx.fillStyle = `hsl(${(frameCount * 5) % 360}, 100%, 50%)`;
      ctx.fillRect(0, 0, 640, 480);
      frameCount++;
    }, 33);

    if (!navigator.mediaDevices) {
      Object.defineProperty(navigator, "mediaDevices", {
        value: { getUserMedia: async () => stream },
        configurable: true,
      });
    } else {
      const originalGetUserMedia =
        navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
      navigator.mediaDevices.getUserMedia = async (constraints) => {
        if (constraints.video) return stream;
        return originalGetUserMedia(constraints);
      };
    }
  });

  page.on("console", (msg) => {
    const text = msg.text();

    // Count ENCRYPTED packet receives
    if (text.includes("[SocketBridge] <<< RECV") && text.includes("pkt_type=1200")) {
      encryptedPackets.push(Date.now());
      console.log(`[TEST] ENCRYPTED packet #${encryptedPackets.length}: ${text}`);
    }

    // Count ASCII_FRAME packets processed
    if (text.includes("ASCII_FRAME PACKET RECEIVED")) {
      asciiFrames.push(Date.now());
      console.log(`[TEST] ASCII_FRAME #${asciiFrames.length} processed`);
    }
  });

  console.log("Starting browser client...");
  const clientUrl = `http://localhost:3000/client?testServerUrl=${encodeURIComponent(serverUrl)}`;
  await page.goto(clientUrl, { waitUntil: "networkidle" });

  console.log("Waiting 3 seconds for frames to arrive...");
  await page.waitForTimeout(3000);

  console.log(`\n========== TEST RESULTS ==========`);
  console.log(`ENCRYPTED packets received: ${encryptedPackets.length}`);
  console.log(`ASCII_FRAME packets processed: ${asciiFrames.length}`);

  // CRITICAL: Must receive multiple frames
  expect(encryptedPackets.length).toBeGreaterThan(
    1,
    `Expected multiple ENCRYPTED packets but got ${encryptedPackets.length}`
  );
  expect(asciiFrames.length).toBeGreaterThan(
    1,
    `Expected multiple ASCII_FRAME packets but got ${asciiFrames.length}`
  );
});
