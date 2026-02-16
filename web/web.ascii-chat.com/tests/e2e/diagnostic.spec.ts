import { test } from "@playwright/test";

test("diagnose UI state", async ({ page }) => {
  await page.goto("http://localhost:5173/client", {
    waitUntil: "domcontentloaded",
  });

  console.log("[Diagnostic] Page loaded");

  // Get all buttons
  const buttons = page.locator("button");
  const count = await buttons.count();
  console.log(`[Diagnostic] Found ${count} buttons`);

  for (let i = 0; i < count; i++) {
    const btn = buttons.nth(i);
    const text = await btn.textContent();
    const style = await btn.evaluate((el) => ({
      backgroundColor: window.getComputedStyle(el).backgroundColor,
      textContent: el.textContent,
      className: el.className,
    }));
    console.log(
      `[Diagnostic] Button ${i}: "${text}" - bg: ${style.backgroundColor}, class: ${style.className}`,
    );
  }

  // Check for status display
  const statusLabels = page.locator("text=Status");
  console.log(`[Diagnostic] Found ${await statusLabels.count()} status labels`);

  // Take screenshot
  await page.screenshot({ path: "/tmp/diagnostic.png", fullPage: true });
  console.log("[Diagnostic] Screenshot saved to /tmp/diagnostic.png");
});
