import { test as base, expect, type Page } from "@playwright/test";

export type BoundaryMeasurement = {
  elapsedMs: number;
  resultBytes: number;
  stableResultIdentity: boolean;
};

export async function measure(page: Page, expression: string, repeats = 32) {
  return page.evaluate(
    ({ expression, repeats }) => {
      const fn = new Function(`return (${expression})`)() as () => unknown;
      fn();
      const start = performance.now();
      let first: unknown;
      let last: unknown;
      let resultBytes = 0;
      for (let i = 0; i < repeats; i++) {
        last = fn();
        first ??= last;
        if (ArrayBuffer.isView(last)) resultBytes += last.byteLength;
      }
      return {
        elapsedMs: performance.now() - start,
        resultBytes,
        stableResultIdentity: first === last,
      } satisfies BoundaryMeasurement;
    },
    { expression, repeats },
  );
}

export const test = base.extend<{
  clientPage: Page;
  rendererPage: Page;
}>({
  clientPage: async ({ page }, use) => {
    await page.goto("/client");
    await use(page);
  },
  rendererPage: async ({ page }, use) => {
    await page.goto("/client");
    await use(page);
  },
});

export { expect };
