import { expect, test } from "@playwright/test";
import { ServerFixture } from "./server-fixture";

// Keep this suite's TCP/WebSocket pair outside the shared random-port range.
// Serial execution lets the three modes reuse one server without worker collisions.
test.describe.configure({ mode: "serial" });

let server: ServerFixture;

test.beforeAll(async () => {
  server = new ServerFixture(38240);
  await server.start();
});

test.afterAll(async () => {
  await server?.stop();
});

for (const renderMode of [0, 1, 2]) {
  test(`render mode ${renderMode} preserves terminal rows after resize`, async ({
    page,
  }) => {
    await page.goto(
      `/client?test&testServerUrl=${encodeURIComponent(server.getUrl())}&settings=true&fit=cover&colorMode=truecolor&renderMode=${renderMode}`,
    );
    await expect(page.locator(".status")).toContainText("Connected", {
      timeout: 20_000,
    });
    // Check the received style before changing settings so a default-mode
    // handshake cannot masquerade as a successful Half-block geometry test.
    await page.waitForFunction((mode) => {
      const frame = window.__clientFrameMetrics?.lastRenderedFrame;
      if (!frame) return false;
      if (mode === 2) return /[\u2580\u2588]/u.test(frame);
      const background = frame.includes("\x1b[48;2;");
      return mode === 1 ? background : !background;
    }, renderMode);
    // The page starts with automatic sizing. Apply a fixed grid once connected.
    await page.getByLabel("Width", { exact: true }).fill("80");
    await page.getByLabel("Height", { exact: true }).fill("24");
    await expect(page.getByText("80x24", { exact: true })).toBeVisible();

    const expectRows = async (rows: number) => {
      await page.waitForFunction((expectedRows) => {
        const frame = window.__clientFrameMetrics?.lastRenderedFrame;
        // Count the received ANSI frame before the terminal rasterizer can
        // clip excess rows. Sample each animation frame because React replaces
        // the diagnostic object when updating the live status bar.
        return !!frame && frame.split("\n").length === expectedRows;
      }, rows);
    };

    await expectRows(24);
    await page.getByLabel("Height", { exact: true }).fill("13");
    await expectRows(13);

    // The server's Matrix state must use the same cell rows in Half-block mode.
    if (renderMode === 2) {
      const rendered = await page.evaluate(
        () => window.__clientFrameMetrics?.rendered ?? 0,
      );
      await page
        .getByRole("button", { name: "Animation", exact: true })
        .click();
      await expect
        .poll(() => page.evaluate(() => window.location.search))
        .toContain("matrixRain=true");
      await expect
        .poll(() =>
          page.evaluate(() => window.__clientFrameMetrics?.rendered ?? 0),
        )
        .toBeGreaterThan(rendered + 2);
      await expectRows(13);
    }

    const canvas = page.locator("canvas.ascii-canvas");
    await expect
      .poll(() =>
        canvas.evaluate((element) => {
          const canvas = element as HTMLCanvasElement;
          const pixels = canvas
            .getContext("2d")!
            .getImageData(0, 0, canvas.width, canvas.height).data;
          return pixels.some((value, index) => index % 4 !== 3 && value > 8);
        }),
      )
      .toBe(true);
  });
}
