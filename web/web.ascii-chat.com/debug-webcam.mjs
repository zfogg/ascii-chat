import { chromium } from "@playwright/test";
import { ServerFixture, getRandomPort } from "./tests/e2e/server-fixture.js";

// Start server
const port = getRandomPort();
const server = new ServerFixture(port);
await server.start();
const serverUrl = server.getUrl();
console.log(`✓ Server started on ${serverUrl}`);

// Launch browser in headed mode
const browser = await chromium.launch({ headless: false });
const context = await browser.newContext();

// Grant permissions before page load
await context.grantPermissions(["camera", "microphone"]);

const page = await context.newPage();

// Capture all console messages
const messages = [];
page.on("console", (msg) => {
  const text = msg.text();
  messages.push(text);
  if (
    text.includes("Setting state to") ||
    text.includes("hash=") ||
    text.includes("Error")
  ) {
    console.log(`[LOG] ${text}`);
  }
});

console.log(`\nOpening browser to ${serverUrl}...`);
const clientUrl = `http://localhost:3000/client?testServerUrl=${encodeURIComponent(serverUrl)}`;
await page.goto(clientUrl, { waitUntil: "networkidle" });

console.log("\nWaiting 10 seconds to observe behavior...");
await page.waitForTimeout(10000);

console.log("\n========== ANALYSIS ==========");
const stateChanges = messages.filter((m) => m.includes("Setting state to"));
console.log(`\nState changes detected: ${stateChanges.length}`);
stateChanges.forEach((msg) => console.log(`  - ${msg}`));

const frames = messages.filter((m) => m.includes("hash="));
console.log(`\nFrame checksums: ${frames.length}`);
frames.slice(0, 5).forEach((msg) => console.log(`  - ${msg}`));

const errors = messages.filter((m) => m.toLowerCase().includes("error"));
console.log(`\nErrors: ${errors.length}`);
errors.slice(0, 5).forEach((msg) => console.log(`  - ${msg}`));

// Take screenshot
await page.screenshot({ path: "/tmp/debug-screenshot.png" });
console.log("\nScreenshot saved");

await browser.close();
await server.stop();
