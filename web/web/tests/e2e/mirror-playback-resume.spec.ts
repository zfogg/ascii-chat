import { expect, test, type Page } from "@playwright/test";

async function expectPlayback(page: Page) {
  await expect(
    page.getByRole("button", { name: "Stop", exact: true }),
  ).toBeVisible();
  await expect
    .poll(() =>
      page.locator("video").evaluate((video) => {
        const stream = (video as HTMLVideoElement)
          .srcObject as MediaStream | null;
        return (
          !(video as HTMLVideoElement).paused &&
          stream?.getVideoTracks().some((track) => track.readyState === "live")
        );
      }),
    )
    .toBe(true);
  const pixels = () =>
    page
      .locator("canvas.ascii-canvas")
      .evaluate((node) => (node as HTMLCanvasElement).toDataURL());
  const first = await pixels();
  await expect.poll(pixels).not.toBe(first);
}

async function fastRefresh(page: Page) {
  // Re-register the component through Vite and run the same React Refresh
  // lifecycle as an HMR update, without rewriting a developer's source file.
  await page.evaluate(async () => {
    const originalUrl = "/src/pages/Mirror.tsx";
    const previous = await import(/* @vite-ignore */ originalUrl);
    const componentUrl = `${originalUrl}?t=${Date.now()}`;
    const next = await import(/* @vite-ignore */ componentUrl);
    const runtimeUrl = "/@react-refresh";
    const runtime = await import(/* @vite-ignore */ runtimeUrl);
    const reason = runtime.validateRefreshBoundaryAndEnqueueUpdate(
      originalUrl,
      previous,
      next,
    );
    if (reason) throw new Error(reason);
  });
}

test("Mirror resumes webcam playback after reload and Fast Refresh", async ({
  page,
}) => {
  test.setTimeout(60_000);
  await page.goto("/mirror?playing=true", { waitUntil: "domcontentloaded" });
  await expectPlayback(page);
  await page.reload({ waitUntil: "domcontentloaded" });
  await expectPlayback(page);
  for (let attempt = 0; attempt < 2; attempt++) {
    const trackId = await page
      .locator("video")
      .evaluate(
        (video) =>
          (
            (video as HTMLVideoElement).srcObject as MediaStream
          ).getVideoTracks()[0]!.id,
      );
    await fastRefresh(page);
    await expect
      .poll(() =>
        page
          .locator("video")
          .evaluate(
            (video) =>
              (
                (video as HTMLVideoElement).srcObject as MediaStream | null
              )?.getVideoTracks()[0]?.id ?? "",
          ),
      )
      .not.toBe(trackId);
    await expectPlayback(page);
    await expect(page).toHaveURL(/playing=true/);
  }
  // Keep checking real media and changing canvas pixels beyond the reported freeze.
  const monitorUntil = Date.now() + 30_000;
  while (Date.now() < monitorUntil) {
    await expectPlayback(page);
    await page.waitForTimeout(1_000);
  }
  await page.getByRole("button", { name: "Stop", exact: true }).click();
  await expect(page).not.toHaveURL(/playing=true/);
  await fastRefresh(page);
  await expect(
    page.getByRole("button", { name: "Start Webcam", exact: true }),
  ).toBeVisible();
  await expect(page.locator("video")).toHaveJSProperty("srcObject", null);
});
