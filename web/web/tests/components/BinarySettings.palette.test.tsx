import { afterEach, expect, test, vi } from "vite-plus/test";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { HeadingProvider } from "@ascii-chat/shared/components";
import { AsciiChatMode } from "../../src/utils/optionsHelp";
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
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );

  const input = screen.getByLabelText("Palette characters");
  expect((input as HTMLInputElement).value).toBe(DEFAULT_PALETTE_CHARS);
  const help = screen.getByRole("button", {
    name: "Palette characters help",
  });
  const tooltipId = help.getAttribute("aria-describedby");
  expect(tooltipId).toBeTruthy();
  expect(document.getElementById(tooltipId!)?.textContent).toBe(
    "Enter characters from darkest to brightest.",
  );

  fireEvent.change(input, { target: { value: " .+#" } });
  expect((input as HTMLInputElement).value).toBe(" .+#");
  act(() => vi.advanceTimersByTime(PALETTE_INPUT_DEBOUNCE_MS - 1));
  expect(onChange).not.toHaveBeenCalled();

  act(() => vi.advanceTimersByTime(1));
  expect(onChange).toHaveBeenCalledOnce();
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    palette: "custom",
    paletteExplicit: true,
    paletteChars: " .+#",
  });
});

test("resets the debounce while the user continues typing", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={AsciiChatMode.MIRROR}
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
    paletteExplicit: true,
    paletteChars: " .#",
  });
});

test.each([
  AsciiChatMode.MIRROR,
  AsciiChatMode.CLIENT,
  AsciiChatMode.DISCOVERY,
])("offers renderer selection before palette in %s mode", (mode) => {
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={mode}
      onChange={onChange}
    />,
    {
      wrapper: HeadingProvider,
    },
  );
  const select = screen.getByLabelText(
    "Text Art Render Algorithm",
  ) as HTMLSelectElement;
  expect(select.value).toBe("luminance");
  fireEvent.change(select, { target: { value: "glyph" } });
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    renderer: "glyph",
  });
  expect(
    select.compareDocumentPosition(screen.getByText("Palette")) &
      Node.DOCUMENT_POSITION_FOLLOWING,
  ).toBeTruthy();
});

test("glyph defaults to its 95-character palette and permits an explicit palette", () => {
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={{ ...DEFAULT_SETTINGS, renderer: "glyph" }}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );
  const palette = screen.getByLabelText("Palette") as HTMLSelectElement;
  expect(palette.value).toBe("glyph-default");
  fireEvent.change(palette, { target: { value: "standard" } });
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    renderer: "glyph",
    palette: "standard",
    paletteExplicit: true,
  });
});

test.each([
  AsciiChatMode.MIRROR,
  AsciiChatMode.CLIENT,
  AsciiChatMode.DISCOVERY,
])("enables stabilization in each mode (%s)", (mode) => {
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={mode}
      onChange={onChange}
    />,
    {
      wrapper: HeadingProvider,
    },
  );
  fireEvent.click(screen.getByRole("checkbox", { name: "Reduce flicker" }));
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    stabilize: true,
  });
});

test("choosing a preset cancels an uncommitted edit", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );
  fireEvent.change(screen.getByLabelText("Palette characters"), {
    target: { value: " .#" },
  });
  fireEvent.change(screen.getByLabelText("Palette"), {
    target: { value: "blocks" },
  });
  act(() => vi.advanceTimersByTime(PALETTE_INPUT_DEBOUNCE_MS));
  expect(onChange).toHaveBeenCalledOnce();
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    palette: "blocks",
    paletteExplicit: true,
  });
});

test("restores the configured palette after an empty edit", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );
  const input = screen.getByLabelText("Palette characters");
  fireEvent.change(input, { target: { value: "" } });
  act(() => vi.advanceTimersByTime(PALETTE_INPUT_DEBOUNCE_MS));
  expect((input as HTMLInputElement).value).toBe(DEFAULT_PALETTE_CHARS);
  expect(onChange).not.toHaveBeenCalled();
});

test("cancels a pending palette edit when settings are disabled", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  const view = render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );
  fireEvent.change(screen.getByLabelText("Palette characters"), {
    target: { value: " .#" },
  });
  view.rerender(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
      disabled
    />,
  );
  act(() => vi.advanceTimersByTime(PALETTE_INPUT_DEBOUNCE_MS));
  expect(onChange).not.toHaveBeenCalled();
  expect(
    (screen.getByLabelText("Palette characters") as HTMLInputElement).value,
  ).toBe(DEFAULT_PALETTE_CHARS);
});
