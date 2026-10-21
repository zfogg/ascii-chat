import { expect, test, type Page } from "./browser-fixture";
import { spawn, type ChildProcess } from "node:child_process";
import { getRandomPort, waitForPort } from "./server-fixture";

interface DisplayTest {
  streams: MediaStream[];
  cancel: boolean;
  calls: unknown[];
}
declare global {
  interface Window {
    displayTest: DisplayTest;
    retainedMicrophone?: MediaStreamTrack | undefined;
  }
}

// Replace only the picker. Real MediaStreams, C/WASM, uploads and visible output run normally.
async function installPicker(page: Page) {
  await page.addInitScript(() => {
    const state: DisplayTest = { streams: [], cancel: false, calls: [] };
    window.displayTest = state;
    navigator.mediaDevices.getDisplayMedia = async (options) => {
      state.calls.push(options);
      if (state.cancel) {
        state.cancel = false;
        throw new DOMException("cancelled", "NotAllowedError");
      }
      const canvas = document.createElement("canvas");
      canvas.width = 640;
      canvas.height = 360;
      const context = canvas.getContext("2d")!;
      const draw = () => {
        context.fillStyle = "red";
        context.fillRect(0, 0, 640, 360);
        context.fillStyle = "white";
        context.font = "bold 90px monospace";
        context.fillText("SCREEN", 40, 170);
        context.fillRect((Date.now() / 15) % 550, 230, 60, 60);
      };
      draw();
      const stream = canvas.captureStream(30);
      const timer = setInterval(draw, 33);
      stream
        .getVideoTracks()[0]!
        .addEventListener("ended", () => clearInterval(timer));
      state.streams.push(stream);
      return stream;
    };
  });
}
async function redPixels(page: Page) {
  return page.locator("canvas.ascii-canvas").evaluate((element) => {
    const canvas = element as HTMLCanvasElement;
    const pixels = canvas
      .getContext("2d")!
      .getImageData(0, 0, canvas.width, canvas.height).data;
    let red = 0;
    for (let i = 0; i < pixels.length; i += 4)
      if (
        pixels[i]! > 50 &&
        pixels[i]! > pixels[i + 1]! * 1.5 &&
        pixels[i]! > pixels[i + 2]! * 1.5
      )
        red++;
    return red;
  });
}

async function fullPageProof(page: Page, file: string) {
  // Full-page capture can expand the viewport. Hold the live renderer geometry
  // for the screenshot so its resize callback cannot clear the canvas mid-shot.
  const surface = page.locator(".ascii-canvas-container");
  const previous = await surface.getAttribute("style");
  await surface.evaluate((element) => {
    const height = element.getBoundingClientRect().height;
    const style = (element as HTMLElement).style;
    style.flex = `0 0 ${height}px`;
    style.height = `${height}px`;
    style.maxHeight = `${height}px`;
  });
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  const upload = page.getByRole("button", { name: "Upload file", exact: true });
  const share = page.getByRole("button", { name: "Share screen", exact: true });
  await expect(share).toHaveClass(/bg-terminal-5/);
  expect(
    await share.evaluate((button) =>
      button.parentElement?.previousElementSibling?.textContent?.trim(),
    ),
  ).toBe("Upload file");
  const uploadBox = await upload.boundingBox(),
    shareBox = await share.boundingBox();
  expect(shareBox!.x).toBeGreaterThanOrEqual(uploadBox!.x + uploadBox!.width);
  expect(Math.abs(shareBox!.y - uploadBox!.y)).toBeLessThan(2);
  await page.screenshot({ path: file, fullPage: true });
  await surface.evaluate((element, style) => {
    if (style === null) element.removeAttribute("style");
    else element.setAttribute("style", style);
  }, previous);
}

test("Mirror renders display ASCII, cancels, replaces, ends and restores the microphone", async ({
  page,
}, info) => {
  await installPicker(page);
  await page.goto("/mirror?colorMode=truecolor");
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled({ timeout: 20000 });
  await page.getByRole("button", { name: "Start Webcam", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(page.getByText("Sharing screen · video only")).toBeVisible();
  expect(await page.locator("video[autoplay]").evaluate((video) => (video as HTMLVideoElement).srcObject instanceof MediaStream && ((video as HTMLVideoElement).srcObject as MediaStream).getAudioTracks().length)).toBe(1);
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  await fullPageProof(page, info.outputPath("mirror-screen-ascii.png"));
  const microphone = await page.evaluate(() => {
    const video = document.querySelector("video[autoplay]") as HTMLVideoElement;
    window.retainedMicrophone = (
      video.srcObject as MediaStream
    ).getAudioTracks()[0];
    return window.retainedMicrophone?.id;
  });
  await page.evaluate(() => {
    window.displayTest.cancel = true;
  });
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled();
  await expect(page.getByText("Sharing screen · video only")).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect
    .poll(() => page.evaluate(() => window.displayTest.streams.length))
    .toBe(2);
  await expect
    .poll(() =>
      page.evaluate(
        () => window.displayTest.streams[0]!.getVideoTracks()[0]!.readyState,
      ),
    )
    .toBe("ended");
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(page.getByText("Sharing screen · video only")).not.toBeVisible();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeVisible();
  await expect
    .poll(() =>
      page.evaluate(() => {
        const video = document.querySelector(
          "video[autoplay]",
        ) as HTMLVideoElement;
        return (video.srcObject as MediaStream)?.getVideoTracks()[0]
          ?.readyState;
      }),
    )
    .toBe("live");
  expect(await page.evaluate(() => window.retainedMicrophone?.readyState)).toBe(
    "live",
  );
  expect(microphone).toBeTruthy();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(page.getByText("Sharing screen · video only")).toBeVisible();
  await page.evaluate(() => {
    const track = window.displayTest.streams.at(-1)!.getVideoTracks()[0]!;
    track.stop();
    track.dispatchEvent(new Event("ended"));
  });
  await expect(page.getByText("Sharing screen · video only")).not.toBeVisible();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Stop", exact: true }).click();
});

test("Mirror screen sharing from idle returns to idle, and file sharing restores the file", async ({
  page,
}) => {
  await installPicker(page);
  await page.goto("/mirror");
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled({ timeout: 20000 });
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(page.getByText("Sharing screen · video only")).toBeVisible();
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Start Webcam", exact: true }),
  ).toBeVisible();
  const image = await page.evaluate(() => {
    const canvas = document.createElement("canvas");
    canvas.width = 320;
    canvas.height = 180;
    const ctx = canvas.getContext("2d")!;
    ctx.fillStyle = "blue";
    ctx.fillRect(0, 0, 320, 180);
    return canvas.toDataURL().split(",")[1]!;
  });
  await page.getByRole("button", { name: "Upload file", exact: true }).click();
  await page.getByLabel("Choose media file").setInputFiles({
    name: "blue.png",
    mimeType: "image/png",
    buffer: Buffer.from(image, "base64"),
  });
  await expect(
    page.getByRole("button", { name: "Toggle Media", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Share screen", exact: true }).click();
  await expect(page.getByText("Sharing screen · video only")).toBeVisible();
  await expect.poll(() => redPixels(page)).toBeGreaterThan(1000);
  await page.getByRole("button", { name: "Stop sharing", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Toggle Media", exact: true }),
  ).toHaveAttribute("aria-pressed", "true");
  await expect.poll(() => redPixels(page)).toBeLessThan(100);
});

for (const mode of ["client", "discovery"]) {
  test(`${mode}: display uploads through live server and preserves microphone`, async ({
    page,
  }, info) => {
    const binary = process.env["ASCII_CHAT_TEST_BINARY"];
    test.skip(
      !binary,
      "Set ASCII_CHAT_TEST_BINARY to a compatible native executable",
    );
    const processes: ChildProcess[] = [];
    let output = "";
    const start = (args: string[]) => {
      const child = spawn(binary!, args, { windowsHide: true });
      processes.push(child);
      child.stdout?.on("data", (data) => {
        output += data;
      });
      child.stderr?.on("data", (data) => {
        output += data;
      });
    };
    const port = getRandomPort();
    const query = new URLSearchParams({
      flipX: "false",
      colorMode: "truecolor",
      encoding: "raw",
    });
    try {
      const server = [
        "server",
        "0.0.0.0",
        "--port",
        String(port),
        "--websocket-port",
        String(port + 1),
        "--status-screen=false",
      ];
      if (mode === "discovery") {
        const acds = port + 1000;
        start([
          "discovery-service",
          "--port",
          String(acds),
          "--websocket-port",
          String(acds + 1),
          "--database",
          info.outputPath("acds.sqlite"),
          "--status-screen=false",
          "--stun-servers",
          "",
          "--turn-servers",
          "",
        ]);
        await waitForPort(acds + 1, "127.0.0.1", { timeoutMs: 20000 });
        server.push(
          "--discovery",
          "--discovery-service",
          "127.0.0.1",
          "--discovery-service-port",
          String(acds),
          "--password",
          "screen-test",
          "--webrtc",
          "--stun-servers",
          "",
          "--turn-servers",
          "",
        );
        query.set("signalingUrl", `ws://127.0.0.1:${acds + 1}`);
        query.set("stunUrls", "");
        query.set("turnUrls", "");
      }
      start(server);
      await waitForPort(port + 1, "127.0.0.1", { timeoutMs: 20000 });
      if (mode === "client")
        query.set("serverUrl", `ws://127.0.0.1:${port + 1}`);
      else {
        await expect
          .poll(
            () =>
              output.match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/i)?.[1] ??
              "",
            { timeout: 30000 },
          )
          .not.toBe("");
        query.set(
          "session",
          output.match(/Session created: ([a-z]+-[a-z]+-[a-z]+)/i)![1]!,
        );
      }
      await installPicker(page);
      await page.goto(`/${mode}?${query}`);
      if (mode === "discovery")
        await page
          .getByLabel("Session password", { exact: true })
          .fill("screen-test");
      await page
        .getByRole("button", {
          name: mode === "client" ? "Connect" : "Join session",
          exact: true,
        })
        .click();
      await expect(
        page.getByRole("button", { name: "Disconnect", exact: true }),
      ).toBeVisible({ timeout: 45000 });
      await page
        .getByRole("button", { name: "Enable speakers", exact: true })
        .click();
      await page
        .getByRole("button", { name: "Enable microphone", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      const before = await page.evaluate(
        () => window.__clientFrameMetrics?.received ?? 0,
      );
      await page
        .getByRole("button", { name: "Share screen", exact: true })
        .click();
      await expect(page.getByText("Sharing screen · video only")).toBeVisible();
      await expect
        .poll(
          () => page.evaluate(() => window.__clientFrameMetrics?.received ?? 0),
          { timeout: 20000 },
        )
        .toBeGreaterThan(before + 3);
      await expect
        .poll(() => redPixels(page), { timeout: 20000 })
        .toBeGreaterThan(1000);
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      await expect(page.getByTestId("audio-levels")).toHaveAttribute(
        "data-sent",
        /[1-9]\d*/,
      );
      const observer = await page.context().newPage();
      await observer.goto(`/${mode}?${query}`);
      if (mode === "discovery")
        await observer
          .getByLabel("Session password", { exact: true })
          .fill("screen-test");
      await observer
        .getByRole("button", {
          name: mode === "client" ? "Connect" : "Join session",
          exact: true,
        })
        .click();
      await expect(
        observer.getByRole("button", { name: "Disconnect", exact: true }),
      ).toBeVisible({ timeout: 45000 });
      await expect(
        observer.getByRole("button", { name: "Disable webcam", exact: true }),
      ).toBeVisible();
      await observer
        .getByRole("button", { name: "Disable webcam", exact: true })
        .click();
      await expect
        .poll(() => redPixels(observer), { timeout: 20000 })
        .toBeGreaterThan(1000);
      await fullPageProof(
        observer,
        info.outputPath(`${mode}-peer-screen-ascii.png`),
      );
      await observer.close();
      await fullPageProof(page, info.outputPath(`${mode}-screen-ascii.png`));
      await page
        .getByRole("button", { name: "Stop sharing", exact: true })
        .click();
      await expect(
        page.getByRole("button", { name: "Share screen", exact: true }),
      ).toBeVisible();
      await expect(
        page.getByRole("button", { name: "Disable microphone", exact: true }),
      ).toBeVisible();
      await page
        .getByRole("button", { name: "Disconnect", exact: true })
        .click();
      expect(
        await page.evaluate(() =>
          window.displayTest.streams.every((stream) =>
            stream
              .getVideoTracks()
              .every((track) => track.readyState === "ended"),
          ),
        ),
      ).toBe(true);
    } finally {
      processes.forEach((child) => child.kill());
    }
  });
}
