import { test, expect } from "@playwright/test";

test("debug mirror 30-second hang - main thread blocking", async ({
  page,
  context,
}, testInfo) => {
  console.log(
    "[HANG-DEBUG] Starting main thread hang detection test at",
    new Date().toISOString(),
  );

  // Grant camera permission so getUserMedia() is called
  await context.grantPermissions(["camera"]);

  const startTime = performance.now();
  const mainThreadLatencies: { time: number; latency: number }[] = [];
  let maxLatency = 0;

  // Collect console messages
  const consoleLogs: { type: string; message: string; time: number }[] = [];

  page.on("console", (msg) => {
    const elapsed = performance.now() - startTime;
    consoleLogs.push({
      type: msg.type(),
      message: msg.text(),
      time: elapsed,
    });
    console.log(`[${elapsed.toFixed(2)}ms] [${msg.type()}] ${msg.text()}`);
  });

  // Inject main thread monitoring script BEFORE navigation
  await page.addInitScript(() => {
    const measurements: Array<{ time: number; latency: number }> = [];
    let measurementCount = 0;

    // Start measuring main thread responsiveness every 100ms
    const interval = setInterval(() => {
      const now = performance.now();
      const expected = measurementCount * 100;
      const latency = now - expected;

      measurements.push({ time: now, latency });
      measurementCount++;

      if (latency > 100) {
        console.error(
          `[MAIN-THREAD-HANG] Latency spike: ${latency.toFixed(0)}ms at ${now.toFixed(0)}ms (expected ~${expected}ms)`,
        );
      }

      // Stop after 40 seconds of monitoring
      if (measurementCount > 400) {
        clearInterval(interval);
        (window as any).__mainThreadMeasurements = measurements;
      }
    }, 100);

    (window as any).__mainThreadMeasurements = measurements;
  });

  // Navigate to mirror page
  console.log(
    "[HANG-DEBUG] Navigating to /mirror with camera permission GRANTED...",
  );
  const navStart = performance.now();

  await page.goto("http://localhost:3000/mirror", {
    waitUntil: "domcontentloaded",
    timeout: 60000,
  });

  const navTime = performance.now() - navStart;
  console.log(`[HANG-DEBUG] Navigation completed in ${navTime.toFixed(2)}ms`);

  // Wait 2 seconds for page to settle, then look for start button or auto-start
  await page.waitForTimeout(2000);
  console.log("[HANG-DEBUG] Checking page state after 2s...");

  // Check if AsciiRenderer canvas is visible (indicates page initialization)
  try {
    const rendererVisible = await page
      .locator("canvas")
      .first()
      .isVisible({ timeout: 1000 })
      .catch(() => false);
    console.log(
      `[HANG-DEBUG] AsciiRenderer canvas visible: ${rendererVisible}`,
    );
  } catch (err) {
    console.log(`[HANG-DEBUG] Could not check canvas visibility: ${err}`);
  }

  // Try to find and click start button, but don't fail if not found (page may auto-start)
  try {
    const startButton = page
      .locator("button")
      .filter({ hasText: /start|webcam/i })
      .first();

    const exists = (await startButton.count()) > 0;
    if (exists) {
      console.log("[HANG-DEBUG] Found start button, clicking...");
      await startButton.click({ timeout: 2000 });
    } else {
      console.log(
        "[HANG-DEBUG] No start button found, monitoring for auto-start",
      );
    }
  } catch (err) {
    console.log(
      `[HANG-DEBUG] Could not interact with start button: ${err}`,
    );
  }

  // Monitor for 40 seconds, collecting main thread latency data
  console.log("[HANG-DEBUG] Monitoring main thread for 40 seconds...");
  let hangDetected = false;
  let hangStartTime = 0;
  let hangEndTime = 0;

  for (let i = 0; i < 40; i++) {
    await page.waitForTimeout(1000);

    try {
      const measurements = await page.evaluate(() => {
        return (window as any).__mainThreadMeasurements || [];
      });

      if (Array.isArray(measurements) && measurements.length > 0) {
        const latest = measurements[measurements.length - 1];
        mainThreadLatencies.push(latest);

        if (latest.latency > maxLatency) {
          maxLatency = latest.latency;
        }

        // Detect hang: if latency > 5000ms, main thread is blocked
        if (latest.latency > 5000 && !hangDetected) {
          hangDetected = true;
          hangStartTime = latest.time;
          console.log(
            `[HANG-DEBUG] HANG DETECTED at ${hangStartTime.toFixed(0)}ms: latency=${latest.latency.toFixed(0)}ms`,
          );
        }

        // Detect end of hang: if latency returns to normal after spike
        if (hangDetected && latest.latency < 200 && hangEndTime === 0) {
          hangEndTime = latest.time;
          const hangDuration = hangEndTime - hangStartTime;
          console.log(
            `[HANG-DEBUG] HANG ENDED after ${hangDuration.toFixed(0)}ms`,
          );
          break; // Stop monitoring once hang is detected and resolved
        }
      }
    } catch (err) {
      console.log(`[HANG-DEBUG] Error reading measurements at ${i}s: ${err}`);
    }
  }

  // Get final measurements
  const finalMeasurements = await page.evaluate(() => {
    return (window as any).__mainThreadMeasurements || [];
  });

  // Analyze console logs for timing gaps
  console.log("\n[HANG-DEBUG] === CONSOLE LOG ANALYSIS ===");
  const relevantLogs = consoleLogs.filter(
    (log) =>
      log.message.includes("[Mirror]") ||
      log.message.includes("[WASM]") ||
      log.message.includes("[AsciiRenderer]") ||
      log.message.includes("[TIMING]") ||
      log.message.includes("[MAIN-THREAD-HANG]") ||
      log.type === "error",
  );

  if (relevantLogs.length === 0) {
    console.log("[HANG-DEBUG] No timing logs found!");
  } else {
    console.log(`[HANG-DEBUG] Found ${relevantLogs.length} relevant logs:\n`);
    let prevTime = 0;
    for (const log of relevantLogs) {
      const gap = log.time - prevTime;
      const gapStr = gap > 100 ? `(+${gap.toFixed(0)}ms GAP)` : "";
      console.log(`  ${log.time.toFixed(2)}ms: ${log.message} ${gapStr}`);
      prevTime = log.time;
    }
  }

  // Report findings
  console.log("\n[HANG-DEBUG] === FINAL REPORT ===");
  console.log(`Main thread max latency: ${maxLatency.toFixed(0)}ms`);
  console.log(`Hang detected: ${hangDetected ? "YES" : "NO"}`);
  if (hangDetected) {
    console.log(
      `  Hang duration: ${(hangEndTime - hangStartTime).toFixed(0)}ms`,
    );
    console.log(`  Started at: ${hangStartTime.toFixed(0)}ms`);
  }
  console.log(
    `Main thread measurements collected: ${finalMeasurements.length}`,
  );
  console.log(`Console logs collected: ${consoleLogs.length}`);

  // Export logs to file for analysis
  const logFile = testInfo.outputPath("hang-debug-logs.json");
  require("fs").writeFileSync(
    logFile,
    JSON.stringify(
      {
        timestamp: new Date().toISOString(),
        hangDetected,
        maxLatency,
        hangStartTime,
        hangEndTime,
        hangDuration: hangDetected ? hangEndTime - hangStartTime : 0,
        mainThreadMeasurements: finalMeasurements,
        consoleLogs: relevantLogs,
      },
      null,
      2,
    ),
  );
  console.log(`[HANG-DEBUG] Logs saved to ${logFile}`);

  // Expect no main thread hang >5 seconds
  expect(maxLatency).toBeLessThan(5000);
});
