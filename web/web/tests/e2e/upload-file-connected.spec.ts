import { spawn, type ChildProcess } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { expect, test } from "./browser-fixture";
import { getRandomPort, waitForPort } from "./server-fixture";

test.use({ viewport: { width: 1600, height: 1000 } });

const proof = path.resolve(
  process.env.UPLOAD_PROOF_DIR ?? "../../build/upload-connected-proof",
);
const binary =
  process.env.ASCII_CHAT_TEST_BINARY ??
  path.resolve(
    `../../build/bin/ascii-chat${process.platform === "win32" ? ".exe" : ""}`,
  );
const media =
  process.env.UPLOAD_TEST_FILE ?? path.resolve("tests/fixtures/media/tone.mp4");

const seekTime = Number(process.env.UPLOAD_TEST_SEEK_SECONDS ?? "1");

for (const mode of ["client", "discovery", "mirror"]) {
  test(`Upload File /${mode}: recognizable file frames ${mode === "mirror" ? "locally" : "through a live server"}`, async ({
    page,
  }) => {
    test.setTimeout(150_000);
    fs.mkdirSync(proof, { recursive: true });
    const children: ChildProcess[] = [];
    fs.writeFileSync(path.join(proof, `${mode}-browser.log`), "");
    let logs = "";
    const start = (name: string, args: string[]) => {
      fs.writeFileSync(path.join(proof, `${mode}-${name}.log`), "");
      const child = spawn(
        binary,
        [
          "--log-level",
          "debug",
          "--log-file",
          path.join(proof, `${mode}-${name}.log`),
          ...args,
        ],
        { windowsHide: true },
      );
      children.push(child);
      child.stdout?.on("data", (chunk) => {
        logs += chunk.toString();
      });
      child.stderr?.on("data", (chunk) => {
        logs += chunk.toString();
      });
      child.on("error", (error) => {
        logs += String(error);
      });
      return child;
    };
    const errors: string[] = [];
    page.on("pageerror", (error) => errors.push(error.message));
    page.on("console", (message) =>
      fs.appendFileSync(
        path.join(proof, `${mode}-browser.log`),
        `${message.type()}: ${message.text()}\n`,
      ),
    );
    try {
      const query = new URLSearchParams({
        flipX: "false",
        colorMode: "truecolor",
      });
      if (mode !== "mirror") {
        const port = getRandomPort();
        const wsPort = port + 1;
        const args = [
          "server",
          "0.0.0.0",
          "--port",
          String(port),
          "--websocket-port",
          String(wsPort),
          "--status-screen=false",
        ];
        if (mode === "discovery") {
          const acdsPort = port + 1000;
          const acdsWs = acdsPort + 1;
          start("acds", [
            "discovery-service",
            "--port",
            String(acdsPort),
            "--websocket-port",
            String(acdsWs),
            "--database",
            path.join(proof, `acds-${acdsPort}.sqlite`),
            "--status-screen=false",
            "--stun-servers",
            "",
            "--turn-servers",
            "",
          ]);
          await waitForPort(acdsWs, "127.0.0.1", { timeoutMs: 20_000 });
          args.push(
            "--discovery",
            "--discovery-service",
            "127.0.0.1",
            "--discovery-service-port",
            String(acdsPort),
            "--password",
            "upload-file-test",
            "--webrtc",
            "--stun-servers",
            "",
            "--turn-servers",
            "",
          );
          query.set("signalingUrl", `ws://127.0.0.1:${acdsWs}`);
          query.set("stunUrls", "");
          query.set("turnUrls", "");
        }
        start("server", args);
        await waitForPort(wsPort, "127.0.0.1", { timeoutMs: 20_000 });
        if (mode === "client")
          query.set("serverUrl", `ws://127.0.0.1:${wsPort}`);
        else {
          let session = "";
          await expect
            .poll(
              () => {
                const file = path.join(proof, `${mode}-server.log`);
                const output =
                  logs +
                  (fs.existsSync(file) ? fs.readFileSync(file, "utf8") : "");
                session =
                  output.match(
                    /Session created: ([a-z]+-[a-z]+-[a-z]+)/i,
                  )?.[1] ?? "";
                return session;
              },
              { timeout: 30_000 },
            )
            .not.toBe("");
          query.set("session", session);
        }
      }
      await page.goto(`/${mode}?${query}`);
      if (mode === "discovery")
        await page
          .getByLabel("Session password", { exact: true })
          .fill("upload-file-test");
      if (mode !== "mirror") {
        await page
          .getByRole("button", {
            name: mode === "client" ? "Connect" : "Join session",
            exact: true,
          })
          .click();
        await expect(
          page.getByRole("button", { name: "Disconnect", exact: true }),
        ).toBeVisible({ timeout: 45_000 });
        await expect(page.locator(".status")).toContainText("Connected");
      }
      await page
        .getByRole("button", { name: "Upload File", exact: true })
        .click();
      await page.getByLabel("Choose media file").setInputFiles(media);
      const video = page.locator("video:not([autoplay])");
      await expect
        .poll(
          () =>
            video.evaluate(
              (element) => (element as HTMLVideoElement).currentTime,
            ),
          { timeout: 90_000 },
        )
        .toBeGreaterThan(0.1);
      await page
        .getByRole("button", {
          name: mode === "mirror" ? "Pause" : "Pause/Play",
          exact: true,
        })
        .click();
      await video.evaluate((element, time) => {
        (element as HTMLVideoElement).currentTime = time;
      }, seekTime);
      await expect
        .poll(() =>
          video.evaluate(
            (element, time) =>
              !(element as HTMLVideoElement).seeking &&
              Math.abs((element as HTMLVideoElement).currentTime - time) < 0.1,
            seekTime,
          ),
        )
        .toBe(true);
      const before = await page.evaluate(
        () => window.__clientFrameMetrics?.received ?? 0,
      );
      if (mode !== "mirror")
        await expect
          .poll(
            () =>
              page.evaluate(() => window.__clientFrameMetrics?.received ?? 0),
            { timeout: 20_000 },
          )
          .toBeGreaterThan(before + 3);
      const asciiCanvas = page.locator("canvas.ascii-canvas");
      await expect(asciiCanvas).toBeVisible();
      await expect
        .poll(
          () =>
            asciiCanvas.evaluate((element) => {
              const surface = element as HTMLCanvasElement;
              const pixels = surface
                .getContext("2d")!
                .getImageData(0, 0, surface.width, surface.height).data;
              let lit = 0;
              for (let i = 0; i < pixels.length; i += 4)
                if (pixels[i]! > 8 || pixels[i + 1]! > 8 || pixels[i + 2]! > 8)
                  lit++;
              return lit;
            }),
          { timeout: 20_000 },
        )
        .toBeGreaterThan(100);
      await page.waitForTimeout(1000);
      const source = await video.evaluate((element) => {
        const video = element as HTMLVideoElement;
        const canvas = document.createElement("canvas");
        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;
        canvas.getContext("2d")!.drawImage(video, 0, 0);
        return canvas.toDataURL("image/png").split(",")[1]!;
      });
      fs.writeFileSync(
        path.join(proof, `${mode}-source.png`),
        Buffer.from(source, "base64"),
      );
      await page.screenshot({
        path: path.join(proof, `${mode}-ascii.png`),
      });
      fs.writeFileSync(
        path.join(proof, `${mode}-metrics.json`),
        JSON.stringify(
          await page.evaluate(() => ({
            metrics: window.__clientFrameMetrics,
            text: document.body.innerText,
          })),
          null,
          2,
        ),
      );
      expect(errors).toEqual([]);
      await page.getByRole("button", { name: "Stop", exact: true }).click();
      if (mode !== "mirror")
        await page
          .getByRole("button", { name: "Disconnect", exact: true })
          .click();
    } finally {
      for (const child of children.reverse()) {
        if (child.exitCode === null) child.kill();
      }
      fs.writeFileSync(path.join(proof, `${mode}-process.log`), logs);
    }
  });
}
