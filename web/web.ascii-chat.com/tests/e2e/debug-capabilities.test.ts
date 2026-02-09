import { test } from "@playwright/test";
import { ServerFixture, getRandomPort } from "./server-fixture";

const TEST_TIMEOUT = 20000; // 20 second timeout for all tests

let server: ServerFixture | null = null;

test.beforeAll(async () => {
  const port = getRandomPort();
  server = new ServerFixture(port);
  await server.start();
  console.log(`✓ Server started for debug-capabilities tests on port ${port}`);
});

test.afterAll(async () => {
  if (server) {
    await server.stop();
    console.log(`✓ Server stopped`);
  }
});

test("Debug CLIENT_CAPABILITIES and ASCII_FRAME flow", async ({
  page,
  context,
}) => {
  test.setTimeout(TEST_TIMEOUT);
  const logs: string[] = [];
  const errors: string[] = [];

  // Grant camera and microphone permissions
  await context.grantPermissions(["camera", "microphone"]);

  // Inject fake video stream mock BEFORE navigating
  await page.addInitScript(() => {
    // Create a canvas for fake video
    const canvas = document.createElement("canvas");
    canvas.width = 640;
    canvas.height = 480;
    const ctx = canvas.getContext("2d");

    // Create a fake video stream
    const stream = canvas.captureStream(30);

    // Draw animated pattern to simulate video
    let frameCount = 0;
    setInterval(() => {
      ctx.fillStyle = `hsl(${(frameCount * 5) % 360}, 100%, 50%)`;
      ctx.fillRect(0, 0, 640, 480);
      ctx.fillStyle = "white";
      ctx.font = "20px Arial";
      ctx.fillText(`Frame: ${frameCount++}`, 20, 30);
    }, 33);

    // Mock navigator.mediaDevices.getUserMedia
    if (!navigator.mediaDevices) {
      Object.defineProperty(navigator, "mediaDevices", {
        value: { getUserMedia: async () => stream },
        configurable: true,
      });
    } else {
      const originalGetUserMedia =
        navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
      navigator.mediaDevices.getUserMedia = async (constraints) => {
        if (constraints.video) {
          return stream;
        }
        return originalGetUserMedia(constraints);
      };
    }
  });

  // Capture all console messages
  page.on("console", (msg) => {
    const type = msg.type();
    const text = msg.text();
    const full = `[${type.toUpperCase()}] ${text}`;

    console.log(full);
    logs.push(full);

    if (type === "error" || text.includes("ERROR")) {
      errors.push(full);
    }
  });

  console.log("\n========== NAVIGATING TO CLIENT PAGE ==========\n");
  await page.goto("http://localhost:3000/client", { waitUntil: "networkidle" });

  console.log(
    "\n========== WAITING FOR CONNECTION AND CAPABILITIES EXCHANGE ==========\n",
  );

  // Wait for CONNECTED state
  try {
    await page.waitForFunction(
      () => {
        const text = document.body.innerText;
        return text.includes("Connected");
      },
      { timeout: 5000 }
    );
    console.log("✅ CONNECTED state reached");
  } catch {
    console.log("⚠️  Timeout waiting for CONNECTED state");
  }

  // Wait for webcam to start and send frames
  await page.waitForTimeout(3000);

  // Try to capture terminal content
  console.log("\n========== CHECKING TERMINAL RENDERING ==========\n");
  try {
    const terminalContent = await page.evaluate(() => {
      const xterm = document.querySelector('.xterm');
      if (!xterm) {
        console.log(`[Playwright] .xterm element NOT found in DOM`);
        return { found: false, content: '' };
      }

      console.log(`[Playwright] .xterm element found`);

      // Check for xterm-screen (DOM-based)
      const xtermScreen = xterm.querySelector('.xterm-screen');
      if (xtermScreen) {
        const rows = xtermScreen.querySelectorAll('.xterm-row');
        console.log(`[Playwright] .xterm-screen found with ${rows.length} rows`);
      }

      // Check for canvas (canvas-based rendering)
      const canvas = xterm.querySelector('canvas');
      if (canvas) {
        console.log(`[Playwright] Canvas found: ${canvas.width}x${canvas.height}`);
      }

      // Try to get terminal text through selection
      const allText = xterm.textContent || '';
      console.log(`[Playwright] Total text content: ${allText.length} chars`);
      if (allText.length > 0) {
        console.log(`[Playwright] First 100 chars: "${allText.substring(0, 100)}"`);
      }

      return { found: true, textLength: allText.length, hasCanvas: !!canvas, hasScreen: !!xtermScreen };
    });
    console.log(`Terminal rendering check:`, terminalContent);
  } catch (e) {
    console.log(`Error checking terminal: ${e}`);
  }

  // Take screenshot to see what's actually rendered
  console.log("\n========== TAKING SCREENSHOT ==========\n");
  await page.screenshot({ path: 'test-screenshot.png', fullPage: true });
  console.log("Screenshot saved to test-screenshot.png");

  console.log("\n\n========== ANALYSIS ==========\n");

  // Parse key events
  const clientCapsSent = logs.filter((l) =>
    l.includes("CLIENT_CAPABILITIES sent successfully")
  );
  const imageFramesSent = logs.filter((l) =>
    l.includes("Sent") && l.includes("IMAGE_FRAME")
  );
  const asciiFramesReceived = logs.filter((l) =>
    l.includes("ASCII_FRAME PACKET RECEIVED")
  );
  const asciiFrameHeaders = logs.filter((l) =>
    l.includes("AsciiFrameParser] Header parsed")
  );
  const rendererWrites = logs.filter((l) => l.includes("WRITE FRAME"));
  const encryptedPackets = logs.filter((l) => l.includes("ENCRYPTED PACKET"));
  const innerPackets = logs.filter((l) => l.includes("Inner packet type"));
  const connectedState = logs.filter((l) =>
    l.includes("CONNECTED state reached")
  );

  console.log(`📊 CONNECTION FLOW:`);
  console.log(`  State transitions to CONNECTED: ${connectedState.length}`);
  if (connectedState.length > 0) console.log(`    → ${connectedState[0]}`);

  console.log(`\n📤 CLIENT_CAPABILITIES SENDING:`);
  console.log(`  Packets sent: ${clientCapsSent.length}`);
  if (clientCapsSent.length > 0) console.log(`    → ${clientCapsSent[0]}`);

  console.log(`\n📹 VIDEO FRAME SENDING:`);
  console.log(`  IMAGE_FRAME packets sent: ${imageFramesSent.length}`);
  if (imageFramesSent.length > 0) console.log(`    → ${imageFramesSent[0]}`);

  console.log(`\n🔐 ENCRYPTION:`);
  console.log(`  ENCRYPTED packets received: ${encryptedPackets.length}`);
  console.log(`  Inner packets decrypted: ${innerPackets.length}`);

  // Show packet type breakdown
  const typeBreakdown = new Map<string, number>();
  innerPackets.forEach((p) => {
    const match = p.match(/type=(\d+) \(([^)]+)\)/);
    if (match) {
      const type = match[2];
      typeBreakdown.set(type, (typeBreakdown.get(type) || 0) + 1);
    }
  });
  console.log(`  Packet types:`);
  typeBreakdown.forEach((count, type) =>
    console.log(`    - ${type}: ${count}`)
  );

  console.log(`\n📺 ASCII_FRAME RECEPTION:`);
  console.log(`  ASCII_FRAME packets: ${asciiFramesReceived.length}`);
  console.log(`  Frame headers parsed: ${asciiFrameHeaders.length}`);
  console.log(`  Renderer writes: ${rendererWrites.length}`);

  // Determine test status
  const hasConnection = connectedState.length > 0;
  const hasCapabilities = clientCapsSent.length > 0;
  const hasEncryption = encryptedPackets.length > 0;
  const hasImageFrames = imageFramesSent.length > 0;
  const hasAsciiFrames = asciiFramesReceived.length > 0;

  console.log(`\n${"═".repeat(60)}`);

  if (!hasConnection) {
    console.log(`❌ FAILED: No connection established`);
  } else if (!hasCapabilities) {
    console.log(`❌ FAILED: CLIENT_CAPABILITIES not sent`);
  } else if (!hasEncryption) {
    console.log(`❌ FAILED: Server not responding with encrypted packets`);
  } else if (!hasImageFrames) {
    console.log(
      `⚠️  INCOMPLETE: CLIENT_CAPABILITIES working, but no IMAGE_FRAME packets`
    );
    console.log(`  This likely means the webcam failed to initialize`);
  } else if (!hasAsciiFrames) {
    console.log(
      `⚠️  INCOMPLETE: Video captured, but server not sending ASCII frames`
    );
    console.log(
      `  Check server logs: palette may not have initialized correctly`
    );
  } else {
    console.log(
      `✅ FULL SUCCESS: Complete CLIENT_CAPABILITIES → IMAGE_FRAME → ASCII_FRAME flow!`
    );
  }

  console.log(`\n📊 Test Results:`);
  console.log(
    `  Connection Established:        ${hasConnection ? "✅" : "❌"}`
  );
  console.log(
    `  CLIENT_CAPABILITIES Sent:      ${hasCapabilities ? "✅" : "❌"} ${hasCapabilities ? `(${clientCapsSent.length} packet)` : ""}`
  );
  console.log(
    `  Server Encrypted Response:     ${hasEncryption ? "✅" : "❌"} ${hasEncryption ? `(${encryptedPackets.length} packets)` : ""}`
  );
  console.log(
    `  IMAGE_FRAME Packets Sent:      ${hasImageFrames ? "✅" : "❌"} ${hasImageFrames ? `(${imageFramesSent.length} packets)` : ""}`
  );
  console.log(
    `  ASCII_FRAME Packets Received:  ${hasAsciiFrames ? "✅" : "❌"} ${hasAsciiFrames ? `(${asciiFramesReceived.length} packets)` : ""}`
  );

  console.log(`${"═".repeat(60)}`);


  if (errors.length > 0) {
    console.log(`\n⚠️  ERRORS:`);
    errors.slice(0, 5).forEach((e) => console.log(`  - ${e}`));
  }

  // Print first 50 logs for full context
  console.log(`\n========== FIRST 50 LOGS FOR CONTEXT ==========\n`);
  logs.slice(0, 50).forEach((l) => console.log(l));
});
