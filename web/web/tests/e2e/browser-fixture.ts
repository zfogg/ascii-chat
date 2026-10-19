import { test as base, type BrowserContext } from "@playwright/test";

// These checks use local assets and servers. External font/CDN requests can
// delay document readiness without contributing to the behavior under test.
export async function blockExternalRequests(context: BrowserContext) {
  await context.route("**/*", (route) => {
    const url = new URL(route.request().url());
    if (
      ["http:", "https:"].includes(url.protocol) &&
      !["localhost", "127.0.0.1", "[::1]"].includes(url.hostname)
    )
      return route.abort();
    return route.continue();
  });
}

export const test = base.extend({
  context: async ({ context }, use) => {
    await blockExternalRequests(context);
    await use(context);
  },
});

export { expect, type Page } from "@playwright/test";
