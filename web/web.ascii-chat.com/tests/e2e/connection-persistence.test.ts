import { test, expect } from "@playwright/test";

test("Client connection persists and renders continuous frames", async ({
  page,
  context,
}) => {
  test.slow();

  const stateChangeLogs: string[] = [];
  const frameUpdateLogs: string[] = [];

  await context.grantPermissions(["camera", "microphone"]);

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
      ctx.fillStyle = "white";
      ctx.font = "20px Arial";
      ctx.fillText(`Frame: ${frameCount++}`, 20, 30);
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

  const terminalSnapshots: string[] = [];

  page.on("console", (msg) => {
    const text = msg.text();
    if (text.includes("State change:")) {
      stateChangeLogs.push(text);
    }
    if (text.includes("WRITE FRAME") || text.includes("ASCII_FRAME PACKET RECEIVED")) {
      frameUpdateLogs.push(text);
    }
  });

  console.log("========== NAVIGATING ==========");
  await page.goto("http://localhost:3000/client", { waitUntil: "networkidle" });

  console.log("========== WAITING 3.5 SECONDS FOR CONTINUOUS FRAMES ==========");

  // Capture terminal content every 0.5 seconds to see if it changes
  for (let i = 0; i < 7; i++) {
    await page.waitForTimeout(500);
    const terminalContent = await page.evaluate(() => {
      // Get the actual rendered text from xterm - look in the rows/text elements
      const rows = document.querySelectorAll(".xterm-rows");
      if (rows.length === 0) return "[xterm not ready]";

      let text = "";
      rows.forEach((row) => {
        const spans = row.querySelectorAll("span");
        spans.forEach((span) => {
          // Get text content, handling ANSI color codes
          text += span.textContent || "";
        });
        text += "\n";
      });

      return text || "[no text content]";
    });
    if (terminalContent) {
      terminalSnapshots.push(terminalContent);
      console.log(`[Snapshot ${i}] Got ${terminalContent.length} chars`);
    }
  }

  console.log("\n========== CHECKING RESULTS ==========");

  // Test 1: Check connection is STILL CONNECTED (not DISCONNECTED or ERROR)
  const hasDisconnect = stateChangeLogs.some(
    (log) => log.includes("State change: 0 (Disconnected)") ||
             log.includes("State change: 4 (Error)")
  );
  const connectCount = stateChangeLogs.filter((l) =>
    l.includes("State change: 3 (Connected)")
  ).length;

  console.log(`Test 1 - Connection persists:`);
  console.log(`  - Has DISCONNECTED/ERROR state: ${hasDisconnect ? "YES ❌" : "NO ✅"}`);
  console.log(`  - CONNECTED state reached: ${connectCount > 0 ? "YES ✅" : "NO ❌"}`);
  console.log(`  - Still connected at 3.5s: ${!hasDisconnect ? "YES ✅" : "NO ❌"}`);

  // Test 2: Check MULTIPLE frames rendered (not just one)
  const frameWriteCount = frameUpdateLogs.filter((l) =>
    l.includes("WRITE FRAME")
  ).length;

  console.log(`\nTest 2 - Continuous rendering:`);
  console.log(`  - Frame writes after initial: ${frameWriteCount}`);
  console.log(`  - Multiple frames rendered (>1): ${frameWriteCount > 1 ? "YES ✅" : "NO ❌"}`);

  // Test 3: Check no connection errors during rendering
  const hasError = frameUpdateLogs.some((l) =>
    l.includes("ERROR") || l.includes("State is ERROR")
  );

  console.log(`\nTest 3 - No errors:`);
  console.log(`  - Errors during frame rendering: ${hasError ? "YES ❌" : "NO ✅"}`);

  // Test 4: Check that frames are ACTUALLY DIFFERENT (not the same frame repeated)
  // Compare terminal snapshots - sum the character codes to get a content hash
  const snapshotHashes = terminalSnapshots.map((snapshot) => {
    let hash = 0;
    for (let i = 0; i < snapshot.length; i++) {
      hash += snapshot.charCodeAt(i);
    }
    return hash;
  });

  const uniqueHashes = new Set(snapshotHashes);

  console.log(`\nTest 4 - Frames are different (terminal content analysis):`);
  console.log(`  - Total snapshots captured: ${terminalSnapshots.length}`);
  console.log(`  - Unique content hashes: ${uniqueHashes.size}`);
  console.log(`  - Are frames changing: ${uniqueHashes.size > 1 ? "YES ✅" : "NO ❌ (same frame repeated)"}`);

  console.log(`\n  - Snapshot hashes: ${Array.from(snapshotHashes).join(", ")}`);
  console.log(`  - First snapshot length: ${terminalSnapshots[0]?.length || 0} chars`);
  if (terminalSnapshots.length > 1) {
    console.log(`  - Second snapshot length: ${terminalSnapshots[1]?.length || 0} chars`);
    console.log(`  - Snapshots identical: ${terminalSnapshots[0] === terminalSnapshots[1] ? "YES" : "NO"}`);
  }

  console.log("\n========== STATE CHANGE LOG ==========");
  stateChangeLogs.forEach((l) => console.log(l));

  console.log("\n========== FRAME UPDATE LOG (first 10) ==========");
  frameUpdateLogs.slice(0, 10).forEach((l) => console.log(l));
  if (frameUpdateLogs.length > 10) {
    console.log(`... and ${frameUpdateLogs.length - 10} more frame updates`);
  }

  console.log("\n========== TERMINAL SNAPSHOTS ==========");
  terminalSnapshots.slice(0, 3).forEach((snap, i) => {
    console.log(`[Snapshot ${i}] Length: ${snap.length}, First 100 chars: ${snap.substring(0, 100)}`);
  });

  // Assertions - test what ACTUALLY matters
  expect(hasDisconnect).toBe(false); // MUST not disconnect
  expect(connectCount).toBeGreaterThan(0); // MUST connect at least once
  expect(frameWriteCount).toBeGreaterThan(1); // MUST have multiple frame updates
  expect(hasError).toBe(false); // MUST not have errors
  expect(uniqueHashes.size).toBeGreaterThan(1); // MUST have different frames, not same repeated
});
