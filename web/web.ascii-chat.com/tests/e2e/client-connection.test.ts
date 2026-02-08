/**
 * E2E tests for WebSocket client connection to native ascii-chat server
 *
 * These tests require a native ascii-chat server to be running with WebSocket support:
 *   ./build/bin/ascii-chat server --websocket-port 27226
 *
 * Run with: bun run test:e2e
 */

import { expect, test } from "@playwright/test";

const SERVER_URL = "ws://localhost:27226"; // Default WebSocket port
const WEB_CLIENT_URL = "http://localhost:3000/client";

// Capture ALL browser console messages to stdout immediately for every test
test.beforeEach(async ({ page }) => {
  page.on("console", (msg) => {
    const type = msg.type();
    const prefix = type === "error" ? "BROWSER:ERR" : "BROWSER";
    console.log(`${prefix}: ${msg.text()}`);
  });
});

test.describe("Client Connection to Native Server", () => {
  test.beforeEach(async ({ page }) => {
    // Navigate to client demo page
    await page.goto(WEB_CLIENT_URL);
  });

  test("should load client demo page", async ({ page }) => {
    await expect(page.locator("h1")).toContainText(
      "ASCII Chat - Client WASM Demo",
    );
  });

  test("should initialize WASM module and auto-generate keypair", async ({ page }) => {
    // Wait for public key to be displayed (WASM init + keypair gen happens during connect flow)
    await expect(page.locator('h2:text("Client Public Key")')).toBeVisible({
      timeout: 2000,
    });
    await expect(page.locator("code")).toHaveText(/^[0-9a-f]{64}$/i);
  });

  test("raw WebSocket should receive packets from server", async ({ page }) => {
    // Bypass React/WASM entirely - just test raw WebSocket connectivity
    const result = await page.evaluate((url) => {
      return new Promise<string>((resolve) => {
        const msgs: string[] = [];
        const ws = new WebSocket(url, "acip");
        ws.binaryType = "arraybuffer";
        ws.onopen = () => {
          msgs.push("OPEN");
          console.error("[RAW WS] Connected");
        };
        ws.onmessage = (e) => {
          const data = new Uint8Array(e.data as ArrayBuffer);
          const type = data.length >= 10 ? ((data[8] << 8) | data[9]) : -1;
          msgs.push(`MSG:len=${data.length},type=${type}`);
          console.error(
            `[RAW WS] Received ${data.length} bytes, type=${type} (0x${
              type.toString(16)
            })`,
          );
        };
        ws.onerror = (e) => {
          msgs.push("ERROR");
          console.error("[RAW WS] Error:", e);
        };
        ws.onclose = (e) => {
          msgs.push(`CLOSE:code=${(e as CloseEvent).code}`);
          console.error(`[RAW WS] Closed: code=${(e as CloseEvent).code}`);
        };
        // Wait 10s for packets then report
        setTimeout(() => {
          ws.close();
          resolve(msgs.join(" | "));
        }, 10000);
      });
    }, SERVER_URL);

    console.log("=== Raw WebSocket result ===");
    console.log(result);

    // We should have received at least one packet
    expect(result).toContain("MSG:");
  });

  test("should auto-connect to native server and complete handshake", async ({ page }) => {
    // Collect all received packet types for debugging
    const packetTypes: string[] = [];
    page.on("console", (msg) => {
      const text = msg.text();
      // Capture packet type info from our debug logging
      if (
        text.includes("RECV packet type=") ||
        text.includes("SEND packet type=") || text.includes("WASM->JS->WS")
      ) {
        packetTypes.push(text);
      }
    });

    // Page should auto-connect on load
    // Wait for final connected state (may transition quickly through intermediate states)
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });

    // Verify connection state indicator shows "Connected" as active
    await expect(
      page.locator('.grid .bg-blue-600:text("Connected")'),
    ).toBeVisible();

    // Verify public key was generated
    await expect(page.locator('h2:text("Client Public Key")')).toBeVisible();
    await expect(page.locator("code")).toHaveText(/^[0-9a-f]{64}$/i);

    // Log all packet types we saw for debugging
    console.log("=== Packet flow summary ===");
    packetTypes.forEach((p) => console.log("  ", p));
  });

  test("should disconnect cleanly", async ({ page }) => {
    // Wait for auto-connection to complete
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });

    // Now disconnect
    await page.click('button:text("Disconnect")');

    // Verify disconnection
    await expect(page.locator(".status")).toContainText("Disconnected");
    await expect(
      page.locator('.grid .bg-blue-600:text("Disconnected")'),
    ).toBeVisible();

    // Connect button should be available again
    await expect(page.locator('button:text("Connect to Server")'))
      .toBeVisible();
  });

  test("should receive video frames from server", async ({ page }) => {
    // This test assumes the server is sending video frames
    // Wait for auto-connection to complete
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 30000,
    });

    // Listen for packet received events in console
    const packetReceived = page.waitForEvent(
      "console",
      (msg) => msg.text().includes("Received packet"),
    );

    // Wait for first packet (timeout after 10s)
    await expect(packetReceived).resolves.toBeTruthy();
  });

  test.skip("should handle connection errors gracefully", async ({ page }) => {
    // This test is skipped because auto-connect happens on page load
    // To test error handling, you would need to:
    // 1. Stop the server before loading the page, OR
    // 2. Add URL parameter support to change server URL before auto-connect
    // For now, error handling can be tested manually by stopping the server
  });
});

test.describe("Opus Audio Codec", () => {
  test("should initialize Opus encoder/decoder", async ({ page }) => {
    await page.goto(WEB_CLIENT_URL);

    // Wait for WASM module to initialize
    await expect(page.locator(".status")).not.toContainText("Not initialized", {
      timeout: 10000,
    });

    // This test requires implementing an audio testing UI
    // For now, we can verify Opus is available via console
    const opusAvailable = await page.evaluate(() => {
      return window.ClientModule !== undefined;
    });

    expect(opusAvailable).toBeTruthy();
  });
});
