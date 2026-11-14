import { afterEach, expect, test, vi } from "vite-plus/test";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { HeadingProvider } from "@ascii-chat/shared/components";
import { defaultGlyphPalette, defaultPalettes } from "@ascii-chat/shared/wasm";
import { AsciiChatMode } from "../../src/utils/optionsHelp";
import { BinarySettings } from "../../src/components/shared/BinarySettings";
import {
  DEFAULT_PALETTE_CHARS,
  DEFAULT_SETTINGS,
  PALETTE_INPUT_DEBOUNCE_MS,
} from "../../src/utils/defaultSettings";

vi.mock("../../src/utils", async (importOriginal) => ({
  ...(await importOriginal<typeof import("../../src/utils")>()),
  getOptionHelp: () => "Setting help",
}));

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

test.each([
  AsciiChatMode.MIRROR,
  AsciiChatMode.CLIENT,
  AsciiChatMode.DISCOVERY,
])("provides visible help for settings with tooltips in %s mode", (mode) => {
  render(
    <BinarySettings
      config={DEFAULT_SETTINGS}
      mode={mode}
      onChange={vi.fn()}
      contentFit="contain"
      onContentFitChange={vi.fn()}
    />,
    { wrapper: HeadingProvider },
  );
  for (const name of [
    "Content fit",
    "Frame Rate",
    "Color Mode",
    "Render Mode",
    "Color Filter",
    "Text Art Render Algorithm",
    "Palette",
    "Animation",
    "Camera Framing",
    "Stabilization",
    "Content Horizontal Flip",
    "Content Vertical Flip",
  ]) {
    expect(screen.getByRole("button", { name: `${name} help` })).toBeTruthy();
  }
});

test("applies custom palette characters after 750 ms", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={{ ...DEFAULT_SETTINGS, palette: "custom" }}
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
    "Select Custom in Palette to edit these characters. Enter custom characters from darkest to brightest.",
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
      config={{ ...DEFAULT_SETTINGS, palette: "custom" }}
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
      config={{ ...DEFAULT_SETTINGS, renderer: "luminance" }}
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
  expect(
    (screen.getByLabelText("Palette characters") as HTMLInputElement).value,
  ).toBe(defaultGlyphPalette);
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
  const toggle = screen.getByRole("button", { name: "Reduce flicker" });
  expect(toggle.getAttribute("aria-pressed")).toBe("true");
  fireEvent.click(toggle);
  expect(onChange).toHaveBeenCalledWith({
    ...DEFAULT_SETTINGS,
    stabilize: false,
    stabilizeExplicit: true,
  });
});

test("choosing a preset cancels an uncommitted edit", () => {
  vi.useFakeTimers();
  const onChange = vi.fn();
  render(
    <BinarySettings
      config={{ ...DEFAULT_SETTINGS, palette: "custom" }}
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
      config={{ ...DEFAULT_SETTINGS, palette: "custom" }}
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
      config={{ ...DEFAULT_SETTINGS, palette: "custom" }}
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
      config={{ ...DEFAULT_SETTINGS, palette: "custom" }}
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

test("shows preset characters and preserves custom characters when switching palettes", () => {
  const onChange = vi.fn();
  const custom = {
    ...DEFAULT_SETTINGS,
    palette: "custom" as const,
    paletteChars: " .+#",
  };
  const view = render(
    <BinarySettings
      config={custom}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
    { wrapper: HeadingProvider },
  );
  const input = screen.getByLabelText("Palette characters") as HTMLInputElement;
  expect(input.disabled).toBe(false);
  expect(input.value).toBe(" .+#");
  for (const [palette, chars] of Object.entries(defaultPalettes)) {
    view.rerender(
      <BinarySettings
        config={{
          ...custom,
          palette: palette as keyof typeof defaultPalettes,
          paletteExplicit: true,
        }}
        mode={AsciiChatMode.MIRROR}
        onChange={onChange}
      />,
    );
    expect(input.disabled).toBe(true);
    expect(input.value).toBe(chars);
  }
  view.rerender(
    <BinarySettings
      config={custom}
      mode={AsciiChatMode.MIRROR}
      onChange={onChange}
    />,
  );
  expect(input.disabled).toBe(false);
  expect(input.value).toBe(" .+#");
});
