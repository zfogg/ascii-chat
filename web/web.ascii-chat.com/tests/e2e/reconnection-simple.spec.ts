import { test, expect } from "@playwright/test";
import { WebSocketServer } from "ws";

test("browser auto-reconnects when server restarts", async ({ page }) => {
  let wsServer: WebSocketServer | null = null;
  const port = 9900 + Math.floor(Math.random() * 100);
  const serverUrl = `ws://localhost:${port}`;

  const startServer = async () => {
    return new Promise<void>((resolve) => {
      wsServer = new WebSocketServer({ port });
      wsServer.on("connection", (ws) => {
        console.log("[Test] Client connected");
        const packet = new Uint8Array(22);
        packet[8] = 0;
        packet[9] = 3; // CRYPTO_HANDSHAKE_COMPLETE
        ws.send(packet.buffer);
      });
      wsServer.once("listening", () => {
        console.log("[Test] Server listening on port", port);
        resolve();
      });
    });
  };

  const stopServer = async () => {
    if (wsServer) {
      for (const ws of wsServer.clients) {
        ws.close();
      }
      await new Promise<void>((resolve) => {
        wsServer!.close(() => resolve());
        setTimeout(() => resolve(), 1000);
      });
      wsServer = null;
      await new Promise((r) => setTimeout(r, 300));
    }
  };

  // Start server and navigate
  console.log("[Test] Starting server...");
  await startServer();

  console.log("[Test] Navigating to client page...");
  await page.goto("http://localhost:5173/client");
  await page.waitForTimeout(500);

  // Click Connection button to open the panel
  console.log("[Test] Clicking Connection button to open panel...");
  const connectionBtn = page
    .locator("button")
    .filter({ hasText: "Connection" });
  await connectionBtn.click();
  await page.waitForTimeout(500);

  // Now look for Connect button to click
  console.log("[Test] Clicking Connect button...");
  const connectBtn = page.locator("button").filter({ hasText: /^Connect$/ });
  await connectBtn.click();
  await page.waitForTimeout(500);

  // Wait for Connected state
  console.log("[Test] Waiting for Connected state...");
  let isConnected = false;
  for (let i = 0; i < 20; i++) {
    const connectedBtn = page
      .locator("button")
      .filter({ hasText: "Connected" });
    isConnected = await connectedBtn
      .evaluate((el) => {
        const style = window.getComputedStyle(el);
        return (
          style.backgroundColor !== "rgb(107, 114, 128)" &&
          style.backgroundColor !== "rgba(0, 0, 0, 0)"
        );
      })
      .catch(() => false);

    if (isConnected) {
      console.log(`[Test] ✓ Connected (attempt ${i + 1})`);
      break;
    }
    await page.waitForTimeout(500);
  }

  await page.screenshot({ path: "/tmp/step1-connected.png", fullPage: true });
  expect(isConnected).toBe(true, "Should show Connected before disconnect");

  // Kill server
  console.log("[Test] Killing server...");
  await stopServer();
  await page.waitForTimeout(1000);

  // Check for Error state (should NOT have it)
  console.log("[Test] Checking for Error state...");
  const errorBtn = page.locator("button").filter({ hasText: /^Error$/ });
  const isError = await errorBtn
    .evaluate((el) => {
      const style = window.getComputedStyle(el);
      return (
        style.backgroundColor !== "rgb(107, 114, 128)" &&
        style.backgroundColor !== "rgba(0, 0, 0, 0)"
      );
    })
    .catch(() => false);

  await page.screenshot({ path: "/tmp/step2-disconnect.png", fullPage: true });
  console.log(`[Test] After disconnect - Error state: ${isError}`);
  expect(isError).toBe(false, "Should NOT show Error state");

  // Wait for Connecting state
  console.log("[Test] Waiting for Connecting state...");
  let isConnecting = false;
  for (let i = 0; i < 5; i++) {
    const connectingBtn = page
      .locator("button")
      .filter({ hasText: "Connecting" });
    isConnecting = await connectingBtn
      .evaluate((el) => {
        const style = window.getComputedStyle(el);
        return (
          style.backgroundColor !== "rgb(107, 114, 128)" &&
          style.backgroundColor !== "rgba(0, 0, 0, 0)"
        );
      })
      .catch(() => false);

    if (isConnecting) {
      console.log(`[Test] ✓ Showing Connecting state (attempt ${i + 1})`);
      break;
    }
    await page.waitForTimeout(500);
  }

  expect(isConnecting).toBe(true, "Should show Connecting state");

  // Restart server
  console.log("[Test] Restarting server...");
  await startServer();
  await page.waitForTimeout(1500);

  // Check for reconnected to Connected state
  console.log("[Test] Waiting for reconnection...");
  isConnected = false;
  for (let i = 0; i < 20; i++) {
    const connectedBtn = page
      .locator("button")
      .filter({ hasText: "Connected" });
    isConnected = await connectedBtn
      .evaluate((el) => {
        const style = window.getComputedStyle(el);
        return (
          style.backgroundColor !== "rgb(107, 114, 128)" &&
          style.backgroundColor !== "rgba(0, 0, 0, 0)"
        );
      })
      .catch(() => false);

    if (isConnected) {
      console.log(`[Test] ✓ Reconnected to Connected (attempt ${i + 1})`);
      break;
    }
    await page.waitForTimeout(300);
  }

  await page.screenshot({ path: "/tmp/step3-reconnected.png", fullPage: true });
  expect(isConnected).toBe(true, "Should reconnect to Connected");

  console.log("[Test] ✅ All checks passed!");
  await stopServer();
});
