import { expect, test } from "./browser-fixture";
import fixture from "./fixtures/errno-stack.json" with { type: "json" };
import { startErrorProxy } from "./fixtures/error-proxy";

test("renders a five-frame errno stack as paired source and message rows", async ({
  page,
}) => {
  const proxy = await startErrorProxy(new URL("./fixtures/errno-stack.json", import.meta.url));

  try {
    await page.goto(
      `/client?serverUrl=${encodeURIComponent(proxy.url)}&settings=true&connect=true`,
    );

    const dialog = page.getByRole("alertdialog", { name: "ascii-chat error" });
    await expect(dialog).toBeVisible();
    await expect(dialog.getByRole("heading")).toHaveText(
      "Remote protocol error 62",
    );

    const list = dialog.locator("ol");
    await expect(dialog.getByText("Stack order: 0 is newest")).toBeVisible();
    await expect(list).toHaveClass(/space-y-4/);
    const rows = list.locator("li");
    await expect(rows).toHaveCount(fixture.frames.length);
    for (let index = 0; index < fixture.frames.length; index++) {
      const { file, line, function: func, message } = fixture.frames[index]!;
      const row = rows.nth(index);
      await expect(row).toHaveClass(/border-red-900/);
      await expect(
        row.locator(
          `[title="${index === 0 ? "Newest error" : `Stack frame ${index}`}"]`,
        ),
      ).toHaveText(String(index));
      await expect(row.locator(".text-blue-400")).toHaveText(file);
      await expect(row.locator(".text-purple-400")).toHaveText(String(line));
      await expect(row.getByText("in", { exact: true })).toHaveClass(
        /text-terminal-fg/,
      );
      await expect(row.locator(".text-orange-400")).toHaveText(`${func}()`);
      await expect(row.locator(".text-red-100")).toHaveText(message);
      await expect(row).not.toContainText("C:/Users/");
    }

    await dialog.getByRole("button", { name: "Dismiss" }).click();
    await expect(dialog).toBeHidden();
  } finally {
    await proxy.close();
  }
});
