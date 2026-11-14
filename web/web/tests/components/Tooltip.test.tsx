import { afterEach, expect, test, vi } from "vite-plus/test";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { Tooltip } from "../../src/components/Tooltip";

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.useRealTimers();
});

test("positions long tooltips using their measured size near the viewport edge", () => {
  vi.useFakeTimers();
  vi.spyOn(HTMLElement.prototype, "getBoundingClientRect").mockImplementation(
    function (this: HTMLElement) {
      return this.getAttribute("role") === "tooltip"
        ? ({ width: 320, height: 220 } as DOMRect)
        : ({ left: 0, bottom: 0 } as DOMRect);
    },
  );
  render(
    <Tooltip text="Long help text">
      <button>Setting</button>
    </Tooltip>,
  );
  fireEvent.mouseEnter(screen.getByRole("button"), {
    clientX: window.innerWidth - 5,
    clientY: window.innerHeight - 5,
  });
  act(() => vi.advanceTimersByTime(350));
  const tooltip = screen.getByRole("tooltip");
  expect(Number.parseFloat(tooltip.style.left)).toBeLessThanOrEqual(
    window.innerWidth - 328,
  );
  expect(Number.parseFloat(tooltip.style.top)).toBeLessThanOrEqual(
    window.innerHeight - 228,
  );
  expect(tooltip.style.visibility).toBe("visible");
});

test("help icons show their tooltip on keyboard focus", () => {
  render(
    <Tooltip text="Help text" id="help" inline showOnFocus>
      <button aria-describedby="help">Help</button>
    </Tooltip>,
  );
  fireEvent.focus(screen.getByRole("button"));
  expect(screen.getByRole("tooltip").textContent).toBe("Help text");
  fireEvent.blur(screen.getByRole("button"));
  expect(screen.queryByRole("tooltip")).toBeNull();
});
