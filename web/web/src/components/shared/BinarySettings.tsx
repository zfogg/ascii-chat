import { useId } from "react";
import { Heading } from "@ascii-chat/shared/components/Heading";
import { getOptionHelp, AsciiChatMode } from "../../utils";
import { Tooltip } from "../Tooltip";
import { LOCKED_SETTINGS_HELP } from "../lockedSettings";

export type ColorMode = "auto" | "none" | "16" | "256" | "truecolor";
export type ColorFilter =
  | "none"
  | "black"
  | "white"
  | "green"
  | "magenta"
  | "fuchsia"
  | "orange"
  | "teal"
  | "cyan"
  | "pink"
  | "red"
  | "yellow"
  | "rainbow";
export type Palette =
  | "standard"
  | "blocks"
  | "digital"
  | "minimal"
  | "cool"
  | "custom";
export type Animation = "matrix" | "waveform" | "fft";

export interface BinarySettingsConfig {
  width: number;
  height: number;
  targetFps: number;
  colorMode: ColorMode;
  colorFilter: ColorFilter;
  palette: Palette;
  paletteChars?: string;
  matrixRain?: boolean;
  zalgo?: number;
  animationEnabled?: boolean;
  animation?: Animation;
  flipX?: boolean;
  flipY?: boolean;
  renderMode?: number;
}

interface BinarySettingsProps {
  config: BinarySettingsConfig;
  onChange: (config: BinarySettingsConfig) => void;
  disabled?: boolean;
  mode: AsciiChatMode;
  contentFit?: "contain" | "cover";
  onContentFitChange?: (fit: "contain" | "cover") => void;
}

function DimensionHelp({ label, text }: { label: string; text: string }) {
  const id = useId();
  return (
    <span className="group relative inline-flex">
      <button
        type="button"
        aria-label={`${label} help`}
        aria-describedby={id}
        className="inline-flex h-4 w-4 items-center justify-center rounded-full border border-terminal-8 text-[10px] text-terminal-8 hover:text-terminal-fg focus-visible:outline focus-visible:outline-terminal-4"
      >
        <span aria-hidden="true">i</span>
      </button>
      <span
        id={id}
        role="tooltip"
        className="pointer-events-none absolute left-0 top-full z-50 mt-2 hidden w-48 rounded bg-gray-800 p-2 text-xs font-normal text-white shadow-lg group-hover:block group-focus-within:block"
      >
        {text}
      </span>
    </span>
  );
}

export function BinarySettings({
  config,
  onChange,
  disabled = false,
  mode,
  contentFit,
  onContentFitChange,
}: BinarySettingsProps) {
  const dimensionsId = useId();
  const audioVisualizationSelected =
    config.animationEnabled &&
    (config.animation === "waveform" || config.animation === "fft");
  const updateConfig = (updates: Partial<BinarySettingsConfig>) => {
    if (!disabled) onChange({ ...config, ...updates });
  };

  return (
    <div
      className={`border-b border-terminal-8 bg-terminal-0 overflow-visible ${disabled ? "settings-locked" : ""}`}
    >
      <div className="px-4 py-3 overflow-visible">
        <Heading
          level={3}
          className="text-sm font-semibold text-terminal-fg mb-3"
        >
          Settings
        </Heading>
        <div className="flex flex-wrap gap-3">
          <div className="flex-1 min-w-[100px]">
            <div className="flex items-center gap-1.5 mb-1">
              <label
                htmlFor={`${dimensionsId}-width`}
                className="text-xs font-medium text-terminal-8"
              >
                Width
              </label>
              <DimensionHelp
                label="Width"
                text={
                  disabled
                    ? LOCKED_SETTINGS_HELP
                    : "Width in character columns. Set to 0 to fit automatically."
                }
              />
            </div>
            <input
              id={`${dimensionsId}-width`}
              type="number"
              min="0"
              max="400"
              value={config.width}
              onChange={(e) =>
                updateConfig({
                  width: Math.min(
                    400,
                    Math.max(0, parseInt(e.target.value) || 0),
                  ),
                })
              }
              disabled={disabled}
              className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
            />
          </div>

          <div className="flex-1 min-w-[100px]">
            <div className="flex items-center gap-1.5 mb-1">
              <label
                htmlFor={`${dimensionsId}-height`}
                className="text-xs font-medium text-terminal-8"
              >
                Height
              </label>
              <DimensionHelp
                label="Height"
                text={
                  disabled
                    ? LOCKED_SETTINGS_HELP
                    : "Height in character rows. Set to 0 to fit automatically."
                }
              />
            </div>
            <input
              id={`${dimensionsId}-height`}
              type="number"
              min="0"
              max="150"
              value={config.height}
              onChange={(e) =>
                updateConfig({
                  height: Math.min(
                    150,
                    Math.max(0, parseInt(e.target.value) || 0),
                  ),
                })
              }
              disabled={disabled}
              className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
            />
          </div>

          {contentFit && onContentFitChange && (
            <Tooltip text="Contain shows the entire image with black bars. Cover fills the canvas and crops the edges.">
              <div className="flex-1 min-w-[100px]">
                <label
                  htmlFor={`${dimensionsId}-fit`}
                  className="block text-xs font-medium text-terminal-8 mb-1"
                >
                  Content fit
                </label>
                <select
                  id={`${dimensionsId}-fit`}
                  value={contentFit}
                  disabled={disabled}
                  onChange={(event) =>
                    onContentFitChange(
                      event.target.value === "cover" ? "cover" : "contain",
                    )
                  }
                  className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
                >
                  <option value="contain">Contain</option>
                  <option value="cover">Cover</option>
                </select>
              </div>
            </Tooltip>
          )}

          <Tooltip
            text={disabled ? LOCKED_SETTINGS_HELP : getOptionHelp(mode, "fps")}
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Frame Rate: {config.targetFps} FPS
              </label>
              <input
                type="range"
                min="15"
                max="60"
                step="15"
                value={config.targetFps}
                onChange={(e) =>
                  updateConfig({ targetFps: parseInt(e.target.value) })
                }
                disabled={disabled}
                className="w-full"
              />
              <div className="flex justify-between text-xs text-terminal-8 mt-1">
                <span>15</span>
                <span>30</span>
                <span>45</span>
                <span>60</span>
              </div>
            </div>
          </Tooltip>

          <Tooltip
            text={
              disabled
                ? LOCKED_SETTINGS_HELP
                : getOptionHelp(mode, "color-mode")
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Color Mode
              </label>
              <select
                value={config.colorMode}
                onChange={(e) =>
                  updateConfig({ colorMode: e.target.value as ColorMode })
                }
                disabled={disabled}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              >
                <option value="auto">Auto</option>
                <option value="none">Monochrome</option>
                <option value="16">16-color</option>
                <option value="256">256-color</option>
                <option value="truecolor">Truecolor (24-bit)</option>
              </select>
            </div>
          </Tooltip>

          <Tooltip
            text={
              disabled
                ? LOCKED_SETTINGS_HELP
                : "Foreground colors the text; Background colors each character cell; Half-block uses two vertically stacked pixels per cell for more detail."
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label
                htmlFor={`${dimensionsId}-render-mode`}
                className="block text-xs font-medium text-terminal-8 mb-1"
              >
                Render Mode
              </label>
              <select
                id={`${dimensionsId}-render-mode`}
                value={config.renderMode ?? 0}
                onChange={(e) =>
                  updateConfig({ renderMode: Number(e.target.value) })
                }
                disabled={disabled}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              >
                <option value={0}>Foreground</option>
                <option value={1}>Background</option>
                <option value={2}>Half-block</option>
              </select>
            </div>
          </Tooltip>

          <Tooltip
            text={
              disabled
                ? LOCKED_SETTINGS_HELP
                : getOptionHelp(mode, "color-filter")
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Color Filter
              </label>
              <select
                value={config.colorFilter}
                onChange={(e) =>
                  updateConfig({ colorFilter: e.target.value as ColorFilter })
                }
                disabled={disabled || audioVisualizationSelected}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              >
                <option value="none">None</option>
                <option value="black">Black</option>
                <option value="white">White</option>
                <option value="green">Green</option>
                <option value="magenta">Magenta</option>
                <option value="fuchsia">Fuchsia</option>
                <option value="orange">Orange</option>
                <option value="teal">Teal</option>
                <option value="cyan">Cyan</option>
                <option value="pink">Pink</option>
                <option value="red">Red</option>
                <option value="yellow">Yellow</option>
                <option value="rainbow">Rainbow (3.5s cycle)</option>
              </select>
            </div>
          </Tooltip>

          <Tooltip
            text={
              disabled ? LOCKED_SETTINGS_HELP : getOptionHelp(mode, "palette")
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Palette
              </label>
              <select
                value={config.palette}
                onChange={(e) =>
                  updateConfig({ palette: e.target.value as Palette })
                }
                disabled={disabled}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              >
                <option value="standard">Standard</option>
                <option value="blocks">Blocks</option>
                <option value="digital">Digital</option>
                <option value="minimal">Minimal</option>
                <option value="cool">Cool</option>
                <option value="custom">Custom</option>
              </select>
            </div>
          </Tooltip>

          <Tooltip
            text={
              disabled ? LOCKED_SETTINGS_HELP : getOptionHelp(mode, "matrix")
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Animation
              </label>
              <button
                onClick={() =>
                  updateConfig({
                    animationEnabled: !config.animationEnabled,
                    matrixRain:
                      !config.animationEnabled &&
                      (config.animation ?? "matrix") === "matrix",
                    colorFilter:
                      !config.animationEnabled &&
                      ((config.animation ?? "matrix") === "waveform" ||
                        (config.animation ?? "matrix") === "fft")
                        ? "none"
                        : config.colorFilter,
                  })
                }
                disabled={disabled}
                className={`w-full px-4 py-2 rounded text-sm font-medium transition-colors ${
                  config.animationEnabled
                    ? "bg-terminal-2 text-terminal-bg hover:bg-terminal-10"
                    : "bg-terminal-8 text-terminal-fg hover:bg-terminal-7"
                }`}
              >
                {config.animationEnabled ? "🟢 Animation" : "Animation"}
              </button>
            </div>
          </Tooltip>

          <div className="flex-1 min-w-[200px]">
            <label
              htmlFor={`${dimensionsId}-zalgo`}
              className="block text-xs font-medium text-terminal-8 mb-1"
            >
              Zalgo
            </label>
            <select
              id={`${dimensionsId}-zalgo`}
              value={config.zalgo ?? 0}
              onChange={(e) => updateConfig({ zalgo: Number(e.target.value) })}
              disabled={disabled}
              className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
            >
              <option value={0}>Off</option>
              {Array.from({ length: 10 }, (_, i) => i + 1).map((level) => (
                <option key={level} value={level}>
                  Level {level}
                  {level === 7 ? " (default)" : ""}
                </option>
              ))}
            </select>
          </div>

          {config.animationEnabled && (
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Animation Selection
              </label>
              <select
                value={config.animation ?? "matrix"}
                onChange={(e) =>
                  updateConfig({
                    animation: e.target.value as Animation,
                    matrixRain: e.target.value === "matrix",
                    colorFilter:
                      e.target.value === "waveform" || e.target.value === "fft"
                        ? "none"
                        : config.colorFilter,
                  })
                }
                disabled={disabled}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              >
                <option value="matrix">Matrix Digital Rain</option>
                <option value="waveform">Audio Waveform</option>
                <option value="fft">Audio Fast Fourier Transform</option>
              </select>
            </div>
          )}

          <Tooltip
            text={
              disabled ? LOCKED_SETTINGS_HELP : getOptionHelp(mode, "flip-x")
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Content Horizontal Flip
              </label>
              <button
                aria-pressed={!!config.flipX}
                onClick={() => updateConfig({ flipX: !config.flipX })}
                disabled={disabled}
                className={`w-full px-4 py-2 border rounded text-sm font-medium transition-colors ${
                  config.flipX
                    ? "border-transparent bg-terminal-2 text-terminal-bg hover:bg-terminal-10"
                    : "bg-terminal-bg text-terminal-fg border-terminal-8 hover:border-terminal-fg"
                }`}
              >
                {config.flipX ? "🟢 Flip Horizontal" : "Flip Horizontal"}
              </button>
            </div>
          </Tooltip>

          <Tooltip
            text={
              disabled ? LOCKED_SETTINGS_HELP : getOptionHelp(mode, "flip-y")
            }
          >
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Content Vertical Flip
              </label>
              <button
                aria-pressed={!!config.flipY}
                onClick={() => updateConfig({ flipY: !config.flipY })}
                disabled={disabled}
                className={`w-full px-4 py-2 border rounded text-sm font-medium transition-colors ${
                  config.flipY
                    ? "border-transparent bg-terminal-2 text-terminal-bg hover:bg-terminal-10"
                    : "bg-terminal-bg text-terminal-fg border-terminal-8 hover:border-terminal-fg"
                }`}
              >
                {config.flipY ? "🟢 Flip Vertical" : "Flip Vertical"}
              </button>
            </div>
          </Tooltip>

          {config.palette === "custom" && (
            <Tooltip
              text={
                disabled ? LOCKED_SETTINGS_HELP : getOptionHelp(mode, "palette")
              }
            >
              <div className="flex-1 min-w-[200px]">
                <label className="block text-xs font-medium text-terminal-8 mb-1">
                  Custom Characters (dark → bright)
                </label>
                <input
                  type="text"
                  value={config.paletteChars || " =#░░▒▒▓▓██"}
                  onChange={(e) =>
                    updateConfig({ paletteChars: e.target.value })
                  }
                  disabled={disabled}
                  placeholder=" =#░░▒▒▓▓██"
                  className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4 font-mono"
                />
                <div className="text-xs text-terminal-8 mt-1">
                  Enter characters from darkest to brightest
                </div>
              </div>
            </Tooltip>
          )}
        </div>
      </div>
    </div>
  );
}
