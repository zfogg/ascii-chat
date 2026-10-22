import { render, screen } from "@testing-library/react";
import { expect, it } from "vite-plus/test";
import { SessionContent } from "../../src/components/SessionContent";

it("keeps the renderer mounted but hidden until loading completes", () => {
  const { rerender } = render(
    <SessionContent>
      <canvas data-testid="raster" />
    </SessionContent>,
  );
  const canvas = screen.getByTestId("raster");
  expect(screen.getByRole("region").getAttribute("aria-busy")).toBe("true");
  expect(canvas.parentElement?.getAttribute("aria-hidden")).toBe("true");
  expect(screen.getByRole("status").textContent).toContain(
    "Preparing your view",
  );
  rerender(
    <SessionContent loading={false}>
      <canvas data-testid="raster" />
    </SessionContent>,
  );
  expect(screen.getByTestId("raster")).toBe(canvas);
  expect(screen.queryByRole("status")).toBeNull();
  expect(canvas.parentElement?.style.visibility).toBe("visible");
});

it("announces failure instead of an endless loader, without internal error text", () => {
  render(
    <SessionContent error="WebAssembly.compile: private implementation detail" />,
  );
  expect(screen.getByRole("alert").textContent).toContain(
    "The view couldn’t load",
  );
  expect(screen.getByRole("alert").textContent).not.toContain("WebAssembly");
  expect(screen.getByRole("button", { name: "Reload page" })).toBeTruthy();
  expect(screen.getByRole("region").getAttribute("aria-busy")).toBe("false");
});
