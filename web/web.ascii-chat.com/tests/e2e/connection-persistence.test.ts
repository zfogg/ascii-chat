import { test, expect } from "@playwright/test";

test("Client connection persists and renders continuous frames", async ({
  page,
  context,
}) => {
  // Use headed mode for debugging
  test.slow(); // This test is slow, give it more time

  const logs: string[] = [];
  const frameContents: string[] = [];

  // Grant permissions
  await context.grantPermissions(["camera", "microphone"]);

  // Inject fake animated video
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
        if (constraints.video) {
          return stream;
        }
        return originalGetUserMedia(constraints);
      };
    }
  });

  // Capture console logs
  page.on("console", (msg) => {
    const text = msg.text();
    console.log(`[${msg.type()}] ${text}`);
    logs.push(text);
  });

  console.log("========== NAVIGATING TO CLIENT PAGE ==========");
  await page.goto("http://localhost:3000/client", {
    waitUntil: "networkidle",
  });

  console.log("========== WAITING FOR FIRST FRAME ==========");
  await page.waitForTimeout(2000);

  // Check connection state at T=2s
  const stateAt2s = await page.evaluate(() => {
    return document.body.innerText;
  });
  console.log("Page content at 2s:", stateAt2s.substring(0, 200));

  // Get first frame content from terminal
  const firstFrameContent = await page.evaluate(() => {
    const xterm = document.querySelector(".xterm");
    return xterm?.textContent?.substring(0, 500) || "";
  });
  frameContents.push(firstFrameContent);
  console.log("First frame content length:", firstFrameContent.length);

  // Wait another 1.5 seconds (total 3.5s from load)
  console.log("========== WAITING 1.5 MORE SECONDS ==========");
  await page.waitForTimeout(1500);

  // Check connection state at T=3.5s
  const stateAt3s = await page.evaluate(() => {
    const text = document.body.innerText;
    const isConnected = text.includes("Connected");
    const isConnecting = text.includes("Connecting");
    const isError = text.includes("Error");
    return { text: text.substring(0, 300), isConnected, isConnecting, isError };
  });

  console.log("Connection state at 3.5s:", stateAt3s);

  // Get second frame content
  const secondFrameContent = await page.evaluate(() => {
    const xterm = document.querySelector(".xterm");
    return xterm?.textContent?.substring(0, 500) || "";
  });
  frameContents.push(secondFrameContent);
  console.log("Second frame content length:", secondFrameContent.length);

  // Check if frames are different
  const framesDifferent = firstFrameContent !== secondFrameContent;
  console.log("Frames are different:", framesDifferent);

  // Wait one more second for more frames
  await page.waitForTimeout(1000);

  const thirdFrameContent = await page.evaluate(() => {
    const xterm = document.querySelector(".xterm");
    return xterm?.textContent?.substring(0, 500) || "";
  });
  frameContents.push(thirdFrameContent);
  console.log("Third frame content length:", thirdFrameContent.length);

  // Count unique frames
  const uniqueFrames = new Set(frameContents).size;
  console.log(`Total frames captured: ${frameContents.length}, Unique: ${uniqueFrames}`);

  // Check metrics
  const connectionCount = logs.filter((l) =>
    l.includes("CONNECTED state reached")
  ).length;
  const asciiFramesCount = logs.filter((l) =>
    l.includes("ASCII_FRAME PACKET RECEIVED")
  ).length;
  const errorCount = logs.filter(
    (l) => l.includes("ERROR") || l.includes("Error")
  ).length;

  console.log(`\n========== RESULTS ==========`);
  console.log(`✅ Connection established: ${connectionCount > 0 ? "YES" : "NO"}`);
  console.log(
    `✅ Still connected at 3.5s: ${stateAt3s.isConnected ? "YES" : "NO"}`
  );
  console.log(
    `✅ ASCII frames received: ${asciiFramesCount} frames`
  );
  console.log(`✅ Frames are animating: ${framesDifferent ? "YES" : "NO"}`);
  console.log(`✅ Unique frames: ${uniqueFrames}/${frameContents.length}`);
  console.log(`⚠️  Errors: ${errorCount}`);

  // Assertions
  expect(connectionCount > 0).toBeTruthy();
  expect(stateAt3s.isConnected).toBeTruthy();
  expect(asciiFramesCount > 0).toBeTruthy();
  expect(framesDifferent).toBeTruthy();
  expect(uniqueFrames > 1).toBeTruthy();
});
