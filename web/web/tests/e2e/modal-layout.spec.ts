import { expect, test } from "@playwright/test";
import { writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

// Alternate silence and quiet microphone input to verify actual level changes.
const rate = 48_000;
const sampleCount = rate * 16;
const wave = Buffer.alloc(44 + sampleCount * 2);
wave.write("RIFF");
wave.writeUInt32LE(wave.length - 8, 4);
wave.write("WAVEfmt ", 8);
wave.writeUInt32LE(16, 16);
wave.writeUInt16LE(1, 20);
wave.writeUInt16LE(1, 22);
wave.writeUInt32LE(rate, 24);
wave.writeUInt32LE(rate * 2, 28);
wave.writeUInt16LE(2, 32);
wave.writeUInt16LE(16, 34);
wave.write("data", 36);
wave.writeUInt32LE(sampleCount * 2, 40);
for (let i = 0; i < sampleCount; i++) {
  const t = i / rate;
  const active = Math.floor(t) % 4 >= 2;
  const amplitude = active ? 300 * (0.8 + 0.2 * Math.sin(t * 6 * Math.PI)) : 0;
  wave.writeInt16LE(
    Math.round(amplitude * Math.sin(t * 880 * Math.PI)),
    44 + i * 2,
  );
}
const microphoneFile = path.join(
  tmpdir(),
  `ascii-chat-modal-meter-${process.pid}.wav`,
);
writeFileSync(microphoneFile, wave);

test.use({
  launchOptions: {
    args: [
      "--use-fake-device-for-media-stream",
      "--use-fake-ui-for-media-stream",
      `--use-file-for-fake-audio-capture=${microphoneFile}`,
    ],
  },
});

const modals = [
  { modal: "device", title: "Device setup", last: "Done" },
  { modal: "crypto", title: "Crypto", last: "Save security settings" },
  { modal: "upload", title: "Upload file", last: "Cancel" },
  { modal: "connection", title: "Connection", last: "Close" },
];

for (const viewport of [
  { width: 320, height: 240 },
  { width: 390, height: 844 },
  { width: 900, height: 320 },
  { width: 1280, height: 900 },
]) {
  for (const { modal, title, last } of modals) {
    test(`${modal}: reachable content and focus at ${viewport.width}x${viewport.height}`, async ({
      page,
    }) => {
      await page.setViewportSize(viewport);
      await page.goto(`/tests/e2e/modal-harness.html?modal=${modal}`);
      const trigger = page.getByRole("button", {
        name: "Open modal",
        exact: true,
      });
      await trigger.click();
      const dialog = page.getByRole("dialog", { name: title, exact: true });
      const panel = dialog.locator(".modal-panel");
      const scroller = dialog.locator(".modal-viewport");
      await expect(panel).toBeVisible();
      if (modal === "crypto") {
        await dialog
          .getByRole("button", { name: "Add private key", exact: false })
          .click();
        await dialog
          .getByRole("button", { name: "Add verification key", exact: false })
          .click();
      }

      await scroller.evaluate((el) => {
        el.scrollTop = 0;
      });
      const box = (await panel.boundingBox())!;
      expect(box.y).toBeGreaterThanOrEqual(0);
      expect(box.x).toBeGreaterThanOrEqual(0);
      expect(box.x + box.width).toBeLessThanOrEqual(viewport.width);
      expect(
        await scroller.evaluate((el) => el.scrollWidth <= el.clientWidth),
      ).toBe(true);

      const footer = dialog.getByRole("button", { name: last, exact: true });
      await footer.scrollIntoViewIfNeeded();
      const footerBox = (await footer.boundingBox())!;
      expect(footerBox.y).toBeGreaterThanOrEqual(0);
      expect(footerBox.y + footerBox.height).toBeLessThanOrEqual(
        viewport.height,
      );
      if (box.height + 32 > viewport.height)
        expect(await scroller.evaluate((el) => el.scrollTop)).toBeGreaterThan(
          0,
        );

      await scroller.hover({ position: { x: 4, y: 4 } });
      await page.mouse.wheel(0, -100_000);
      await expect.poll(() => scroller.evaluate((el) => el.scrollTop)).toBe(0);
      const heading = dialog.getByRole("heading", { name: title, exact: true });
      const headingBox = (await heading.boundingBox())!;
      expect(headingBox.y).toBeGreaterThanOrEqual(0);
      expect(headingBox.y + headingBox.height).toBeLessThanOrEqual(
        viewport.height,
      );
      expect(await page.evaluate(() => window.scrollY)).toBe(0);

      await footer.focus();
      await page.keyboard.press("Tab");
      expect(
        await dialog.evaluate((el) => el.contains(document.activeElement)),
      ).toBe(true);
      await page.keyboard.press("Escape");
      await expect(panel).toBeHidden();
      await expect(trigger).toBeFocused();
    });
  }
}

test("device meter responds to microphone input without a separate start toggle", async ({
  page,
}) => {
  test.setTimeout(30_000);
  await page.goto("/mirror");
  await page.getByRole("button", { name: "Device setup", exact: true }).click();
  const meter = page.getByRole("meter", {
    name: "Microphone input level",
    exact: true,
  });
  await expect(
    page.getByRole("button", { name: "Stop test", exact: true }),
  ).toBeEnabled();
  const levels: number[] = [];
  for (let i = 0; i < 60; i++) {
    levels.push(Number(await meter.getAttribute("aria-valuenow")));
    await page.waitForTimeout(100);
  }
  expect(Math.max(...levels)).toBeGreaterThan(15);
  expect(Math.min(...levels)).toBeLessThan(3);
  await page.getByRole("button", { name: "Stop test", exact: true }).click();
  await expect(meter).toHaveAttribute("aria-valuenow", "0");
  await expect(meter.locator("div")).toHaveCSS("width", "0px");
  await page
    .getByRole("button", { name: "Test microphone", exact: true })
    .click();
  await expect
    .poll(async () => Number(await meter.getAttribute("aria-valuenow")), {
      timeout: 8_000,
    })
    .toBeGreaterThan(15);
  await page.getByRole("button", { name: "Done", exact: true }).click();
  await expect(meter).toHaveCount(0);
});
