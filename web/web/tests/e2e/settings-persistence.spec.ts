import {
  blockExternalRequests,
  expect,
  test,
  type Page,
} from "./browser-fixture";

async function colorControl(page: Page) {
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  return page.locator("select").filter({
    has: page.locator('option[value="truecolor"]'),
  });
}

test("render settings survive reload and a copied URL in a fresh context", async ({
  page,
  browser,
}) => {
  await page.goto("/mirror?unrelated=kept");
  const color = await colorControl(page);
  await color.selectOption("none");
  await expect(page).toHaveURL(/colorMode=none/);
  const savedUrl = page.url();
  expect(new URL(savedUrl).searchParams.get("unrelated")).toBe("kept");
  await page.reload();
  await expect(await colorControl(page)).toHaveValue("none");
  const context = await browser.newContext();
  try {
    await blockExternalRequests(context);
    const copied = await context.newPage();
    await copied.goto(savedUrl);
    const restored = await colorControl(copied);
    await expect(restored).toHaveValue("none");
    await restored.selectOption("truecolor");
    await expect
      .poll(() => new URL(copied.url()).searchParams.has("colorMode"))
      .toBe(false);
    expect(new URL(copied.url()).searchParams.get("unrelated")).toBe("kept");
  } finally {
    await context.close();
  }
});
