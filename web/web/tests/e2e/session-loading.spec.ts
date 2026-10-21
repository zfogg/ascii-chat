import { test, expect, type Page } from "./browser-fixture";

async function centeredInContent(page: Page) {
  const box = page.getByRole("region", { name: "ASCII view", exact: true });
  const message = box.locator(".session-loading-message");
  await expect(message).toBeVisible();
  const frame = await box.boundingBox();
  const content = await message.boundingBox();
  const header = await page.getByRole("banner").boundingBox();
  expect(frame).not.toBeNull();
  expect(content).not.toBeNull();
  expect(frame!.y).toBeGreaterThanOrEqual(header!.y + header!.height - 1);
  expect(
    Math.abs(frame!.x + frame!.width / 2 - (content!.x + content!.width / 2)),
  ).toBeLessThan(2);
  expect(
    Math.abs(frame!.y + frame!.height / 2 - (content!.y + content!.height / 2)),
  ).toBeLessThan(2);
  expect(frame!.height).toBeGreaterThanOrEqual(
    Math.min(448, page.viewportSize()!.height * 0.65) - 1,
  );
  expect(
    await page.evaluate(() => document.documentElement.scrollWidth),
  ).toBeLessThanOrEqual(page.viewportSize()!.width);
}

for (const viewport of [
  { width: 1500, height: 1000 },
  { width: 390, height: 844 },
]) {
  for (const mode of ["mirror", "client", "discovery"]) {
    test(`${mode}: page loading stays centered in its content box at ${viewport.width}px`, async ({
      page,
    }, info) => {
      await page.setViewportSize(viewport);
      let release!: () => void;
      const gate = new Promise<void>((resolve) => {
        release = resolve;
      });
      await page.route(
        `**/src/pages/${mode === "mirror" ? "Mirror" : mode === "client" ? "Client" : "Discovery"}.tsx`,
        async (route) => {
          await gate;
          await route.continue();
        },
      );
      try {
        await page.goto(`/${mode}`, { waitUntil: "domcontentloaded" });
        await expect(
          page.getByText("Opening your workspace", { exact: true }),
        ).toBeVisible();
        await centeredInContent(page);
        await page.screenshot({
          path: info.outputPath(`${mode}-page-loading-${viewport.width}.png`),
          fullPage: true,
        });
      } finally {
        release();
      }
      await expect(
        page.getByText("Opening your workspace", { exact: true }),
      ).not.toBeVisible();
    });

    test(`${mode}: delayed ASCII view stays centered below controls at ${viewport.width}px`, async ({
      page,
    }, info) => {
      await page.setViewportSize(viewport);
      let release!: () => void;
      const gate = new Promise<void>((resolve) => {
        release = resolve;
      });
      const wasm = mode === "mirror" ? "mirror" : "client";
      await page.route(`**/wasm/**/${wasm}.wasm`, async (route) => {
        await gate;
        await route.continue();
      });
      try {
        await page.goto(
          `/${mode}${mode === "mirror" ? "" : "?connect=true&serverUrl=ws%3A%2F%2F127.0.0.1%3A1&session=swift-river-mountain"}`,
          { waitUntil: "domcontentloaded" },
        );
        if (mode !== "mirror") {
          // Saved connect intent requests the renderer, independently of the
          // deliberately unavailable server. Do not manufacture a delay in UI.
          const connect = page.getByRole("button", {
            name: mode === "client" ? "Connect" : "Join session",
            exact: true,
          });
          if (await connect.isVisible()) await connect.click();
        }
        await expect(
          page.getByText("Preparing your view", { exact: true }),
        ).toBeVisible();
        await centeredInContent(page);
        await expect(
          page.getByRole("banner").getByRole("link").first(),
        ).toBeVisible();
        await page.screenshot({
          path: info.outputPath(`${mode}-view-loading-${viewport.width}.png`),
          fullPage: true,
        });
      } finally {
        release();
      }
      await expect(
        page.getByText("Preparing your view", { exact: true }),
      ).not.toBeVisible({ timeout: 45000 });
    });
  }
}

test("failed view load gives a truthful inline retry and recovers after reload", async ({
  page,
}, info) => {
  await page.route("**/wasm/**/mirror.wasm", (route) => route.abort("failed"));
  await page.goto("/mirror");
  await expect(page.getByRole("alert")).toContainText("The view couldn’t load");
  await expect(
    page.getByRole("region", { name: "ASCII view", exact: true }),
  ).toHaveAttribute("aria-busy", "false");
  await centeredInContent(page);
  await page.screenshot({
    path: info.outputPath("mirror-load-error.png"),
    fullPage: true,
  });
  await page.unroute("**/wasm/**/mirror.wasm");
  await page.getByRole("button", { name: "Reload page", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Share screen", exact: true }),
  ).toBeEnabled();
  await expect(page.getByRole("alert")).not.toBeVisible();
});
