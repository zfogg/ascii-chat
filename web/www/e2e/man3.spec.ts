import { test, expect } from "@playwright/test";

test.describe("Man3 Pages", () => {
  test("page loads and displays search", async ({ page }) => {
    await page.goto("/man3");
    const searchInput = page.locator("input").first();
    await expect(searchInput).toBeVisible();
  });

  test("can search pages", async ({ page }) => {
    await page.goto("/man3");
    const searchInput = page.locator("input").first();

    // Search for "acds_client_t"
    await searchInput.fill("acds_client_t");
    await page.waitForTimeout(2000);

    // All results should be relevant to the search query
    const allResults = page.locator(
      'button[class*="w-full"][class*="text-left"]',
    );
    const resultCount = await allResults.count();

    // Check that all results contain the search term
    for (let i = 0; i < Math.min(resultCount, 5); i++) {
      const resultText = await allResults.nth(i).textContent();
      // Every result should be related to "acds_client_t"
      expect(resultText).toContain("acds_client_t");
    }
  });

  test("can navigate to a page", async ({ page }) => {
    await page.goto("/man3");
    const searchInput = page.locator("input").first();
    await searchInput.fill("acds");
    await page.waitForTimeout(1500);

    // Find first clickable button in the results area (has class w-full)
    const firstButton = page
      .locator('button[class*="w-full"][class*="text-left"]')
      .first();
    await firstButton.click();
    await page.waitForTimeout(500);

    expect(page.url()).toContain("page=");
  });

  test("page with query parameter loads", async ({ page }) => {
    await page.goto("/man3?page=acds");
    await page.waitForTimeout(300);
    const content = page.locator("body");
    await expect(content).toBeVisible();
  });

  test("search is case-insensitive", async ({ page }) => {
    await page.goto("/man3");
    const searchInput = page.locator("input").first();

    await searchInput.fill("acds");
    await page.waitForTimeout(1500);
    const content1 = await page.textContent("body");
    const count1 = (content1?.match(/acds/gi) || []).length;

    await searchInput.clear();
    await searchInput.fill("ACDS");
    await page.waitForTimeout(1500);
    const content2 = await page.textContent("body");
    const count2 = (content2?.match(/ACDS/gi) || []).length;

    expect(count1).toBe(count2);
  });

  test("can clear search", async ({ page }) => {
    await page.goto("/man3");
    const searchInput = page.locator("input").first();

    await searchInput.fill("xyz");
    await page.waitForTimeout(200);

    await searchInput.clear();
    await page.waitForTimeout(200);

    const value = await searchInput.inputValue();
    expect(value).toBe("");
  });

  test("back/forward navigation works", async ({ page }) => {
    await page.goto("/man3");
    // First navigate to a specific page
    const searchInput = page.locator("input").first();
    await searchInput.fill("acds");
    await page.waitForTimeout(1500);
    const firstButton = page
      .locator('button[class*="w-full"][class*="text-left"]')
      .first();
    await firstButton.click();
    await page.waitForTimeout(1000);

    const _url1 = page.url();

    // Navigate to another page
    await searchInput.fill("acip");
    await page.waitForTimeout(1500);
    const secondButton = page
      .locator('button[class*="w-full"][class*="text-left"]')
      .first();
    const isVisible = await secondButton.isVisible().catch(() => false);
    if (isVisible) {
      await secondButton.click();
      await page.waitForTimeout(1000);

      // Go back
      await page.goBack();
      await page.waitForTimeout(1000);

      // Check that we're back on a page with "acds" in the URL
      expect(page.url()).toContain("acds");
    }
  });

  test("mobile viewport works", async ({ page }) => {
    await page.setViewportSize({ width: 375, height: 667 });
    await page.goto("/man3");

    const searchInput = page.locator("input").first();
    await expect(searchInput).toBeVisible();
  });

  test("rapid navigation is stable", async ({ page }) => {
    await page.goto("/man3");

    const searchInput = page.locator("input").first();
    await searchInput.fill("acds");
    await page.waitForTimeout(1500);

    // Click a few result buttons rapidly
    const resultButtons = page.locator(
      'button[class*="w-full"][class*="text-left"]',
    );
    const count = await resultButtons.count();

    for (let i = 0; i < Math.min(count, 2); i++) {
      await resultButtons.nth(i).click();
      await page.waitForTimeout(300);
    }

    expect(page.url()).toContain("man3");
  });
});
