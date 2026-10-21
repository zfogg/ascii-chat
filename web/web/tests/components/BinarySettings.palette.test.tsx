import { afterEach, expect, test, vi } from "vite-plus/test";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { HeadingProvider } from "@ascii-chat/shared/components";
import { BinarySettings } from "../../src/components/shared/BinarySettings";
import {
  DEFAULT_PALETTE_CHARS,
  DEFAULT_SETTINGS,
  PALETTE_INPUT_DEBOUNCE_MS,
} from "../../src/utils/defaultSettings";

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

test("shows the engine standard palette and applies typed characters after 750 ms", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode="mirror"
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );

  const input = screen.getByLabelText("Palette characters");
  expect((input as HTMLInputElement).value).toBe(DEFAULT_PALETTE_CHARS);

  fireEvent.change(input, { target: { value: " .+#" } });
  expect((input as HTMLInputElement).value).toBe(" .+#");
  act(() => vi.advanceTimersByTime(PALETTE_INPUT_DEBOUNCE_MS - 1));
  expect(onChange).not.toHaveBeenCalled();

  act(() => vi.advanceTimersByTime(1));
  expect(onChange).toHaveBeenCalledOnce();
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    palette: "custom",
    paletteChars: " .+#",
  });
});

test("resets the debounce while the user continues typing", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode="mirror"
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );

  const input = screen.getByLabelText("Palette characters");
  fireEvent.change(input, { target: { value: " ." } });
  act(() => vi.advanceTimersByTime(500));
  fireEvent.change(input, { target: { value: " .#" } });
  act(() => vi.advanceTimersByTime(500));
  expect(onChange).not.toHaveBeenCalled();
  act(() => vi.advanceTimersByTime(250));
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    palette: "custom",
    paletteChars: " .#",
  });
});
