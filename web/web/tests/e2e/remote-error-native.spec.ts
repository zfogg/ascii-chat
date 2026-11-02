import { test, expect } from "./browser-fixture";
import { ServerFixture, getRandomPort } from "./server-fixture";

for (const encryption of [false, true]) {
  test(`native ${encryption ? "encrypted" : "plain"} WebSocket receives browser errors and a full-session browser recovers`, async ({
    page,
    context,
  }) => {
    test.skip(
      !process.env["ASCII_CHAT_TEST_BINARY"],
      "Requires a native ascii-chat executable",
    );
    test.setTimeout(90000);
    const server = new ServerFixture(getRandomPort(), [
      ...(encryption ? [] : ["--no-encrypt", "--no-auth"]),
      "--max-clients",
      "1",
      "--status-screen=false",
    ]);
    const waiting = await context.newPage();
    const connect = async (target: import("@playwright/test").Page) => {
      await target.goto("/client");
      await target.evaluate(
        async ({ serverUrl, encryption }) => {
          const url = "/src/network/ClientConnection.ts";
          const { ClientConnection } = await import(/* @vite-ignore */ url);
          const buildersUrl = "/src/network/packetBuilders.ts";
          const { buildCapabilitiesPacket } = await import(
            /* @vite-ignore */ buildersUrl
          );
          window.__nativeErrorConnection = new ClientConnection({
            serverUrl,
            applicationEncryption: encryption,
          });
          window.__nativeErrorConnection!.onStateChange((state: number) => {
            if (state === 3)
              window.__nativeErrorConnection!.sendPacket(
                5000,
                buildCapabilitiesPacket(40, 12, 1, "none"),
              );
          });
          window.__nativeErrorConnection!.onPacketReceived((packet) => {
            if (packet.type === 5002) window.__nativePongs++;
          });
          window.__nativePongs = 0;
          await window.__nativeErrorConnection!.connect();
        },
        { serverUrl: server.getUrl(), encryption },
      );
    };
    try {
      await server.start();
      await connect(page);
      await expect
        .poll(
          () => page.evaluate(() => window.__nativeErrorConnection!.getState()),
          { timeout: 20000 },
        )
        .toBe(3);
      await page.evaluate(async () => {
        const url = "/src/errors/errorBus.ts";
        const { forwardJavaScriptError } = await import(/* @vite-ignore */ url);
        forwardJavaScriptError(new Error("BROWSER_PRIVATE_DETAIL"));
      });
      await expect
        .poll(() => server.logs)
        .toMatch(/Client \S+ reported Internal server error/);
      expect(server.logs).not.toContain("BROWSER_PRIVATE_DETAIL");
      await page.evaluate(() =>
        window.__nativeErrorConnection!.sendPacket(5001, new Uint8Array()),
      );
      await expect
        .poll(() => page.evaluate(() => window.__nativePongs))
        .toBeGreaterThan(0);
      await connect(waiting);
      const dialog = waiting.getByRole("alertdialog", {
        name: "ascii-chat error",
      });
      await expect(dialog).toBeVisible();
      await expect(dialog).toContainText("Remote protocol error 48");
      await page.evaluate(() => window.__nativeErrorConnection!.disconnect());
      await expect
        .poll(
          () =>
            waiting.evaluate(() => window.__nativeErrorConnection!.getState()),
          { timeout: 20000 },
        )
        .toBe(3);
      await waiting.evaluate(() =>
        window.__nativeErrorConnection!.sendPacket(5001, new Uint8Array()),
      );
      await expect
        .poll(() => waiting.evaluate(() => window.__nativePongs))
        .toBeGreaterThan(0);
      await expect(dialog).toBeVisible();
      await dialog.getByRole("button", { name: "Dismiss" }).click();
      await expect(dialog).toBeHidden();
    } finally {
      await page.evaluate(() => window.__nativeErrorConnection?.disconnect());
      await waiting.evaluate(() =>
        window.__nativeErrorConnection?.disconnect(),
      );
      await waiting.close();
      await server.stop();
    }
  });
}

declare global {
  interface Window {
    __nativeErrorConnection?: import("../../src/network/ClientConnection").ClientConnection;
    __nativePongs: number;
  }
}
