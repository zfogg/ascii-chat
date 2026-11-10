import { test as base, expect, type Page } from "@playwright/test";

export type BoundaryMeasurement = {
  elapsedMs: number;
  calls: number;
  resultBytes: number;
  stableResultIdentity: boolean;
  allResultsUnique: boolean;
};

export async function measure(page: Page, expression: string, repeats = 32) {
  return page.evaluate(
    ({ expression, repeats }) => {
      const fn = new Function(`return (${expression})`)() as () => unknown;
      const warmup = fn();
      if (!(warmup instanceof Uint8ClampedArray) || warmup.length === 0)
        throw new Error("Renderer warmup returned no pixel data");
      const start = performance.now();
      let first: unknown;
      let last: unknown;
      let stableResultIdentity = true;
      const results = new Set<unknown>();
      let resultBytes = 0;
      for (let i = 0; i < repeats; i++) {
        last = fn();
        if (!(last instanceof Uint8ClampedArray) || last.length === 0)
          throw new Error(`Renderer returned no pixel data on iteration ${i}`);
        first ??= last;
        stableResultIdentity &&= first === last;
        results.add(last);
        resultBytes += last.byteLength;
      }
      return {
        elapsedMs: performance.now() - start,
        calls: repeats,
        resultBytes,
        stableResultIdentity,
        allResultsUnique: results.size === repeats,
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
