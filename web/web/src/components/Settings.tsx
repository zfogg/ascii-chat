import { Heading } from "@ascii-chat/shared/components";
import { getOptionHelp, AsciiChatMode } from "../utils/optionsHelp";
import { Tooltip } from "./Tooltip";

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

export interface SettingsConfig {
  width: number;
  height: number;
  targetFps: number;
  colorMode: ColorMode;
  colorFilter: ColorFilter;
  palette: Palette;
  paletteChars?: string;
  matrixRain?: boolean;
  flipX?: boolean;
}

interface SettingsProps {
  config: SettingsConfig;
  onChange: (config: SettingsConfig) => void;
  disabled?: boolean;
  mode: AsciiChatMode;
}

export function Settings({
  config,
  onChange,
  disabled = false,
  mode,
}: SettingsProps) {
  const updateConfig = (updates: Partial<SettingsConfig>) => {
    onChange({ ...config, ...updates });
  };

  return (
    <div className="border-b border-terminal-8 bg-terminal-0 overflow-visible">
      <div className="px-4 py-3 overflow-visible">
        <Heading
          level={3}
          className="text-sm font-semibold text-terminal-fg mb-3"
        >
          Settings
        </Heading>
        <div className="flex flex-wrap gap-3">
          {/* Width Field */}
          <Tooltip text={getOptionHelp(mode, "width")}>
            <div className="flex-1 min-w-[100px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Width
              </label>
              <input
                type="number"
                min="1"
                max="2560"
                value={config.width}
                onChange={(e) =>
                  updateConfig({ width: parseInt(e.target.value) || 0 })
                }
                disabled={disabled}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              />
            </div>
          </Tooltip>

          {/* Height Field */}
          <Tooltip text={getOptionHelp(mode, "height")}>
            <div className="flex-1 min-w-[100px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Height
              </label>
              <input
                type="number"
                min="1"
                max="2560"
                value={config.height}
                onChange={(e) =>
                  updateConfig({
                    height: parseInt(e.target.value) || 0,
                  })
                }
                disabled={disabled}
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
              />
            </div>
          </Tooltip>

          {/* Frame Rate */}
          <Tooltip text={getOptionHelp(mode, "fps")}>
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

          {/* Color Mode */}
          <Tooltip text={getOptionHelp(mode, "color-mode")}>
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

          {/* Color Filter */}
          <Tooltip text={getOptionHelp(mode, "color-filter")}>
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Color Filter
              </label>
              <select
                value={config.colorFilter}
                onChange={(e) =>
                  updateConfig({ colorFilter: e.target.value as ColorFilter })
                }
                disabled={disabled}
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

          {/* Palette */}
          <Tooltip text={getOptionHelp(mode, "palette")}>
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

          {/* Matrix Rain Effect */}
          <Tooltip text={getOptionHelp(mode, "matrix")}>
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Effects
              </label>
              <button
                onClick={() => updateConfig({ matrixRain: !config.matrixRain })}
                disabled={disabled}
                className={`w-full px-4 py-2 rounded text-sm font-medium transition-colors ${
                  config.matrixRain
                    ? "bg-terminal-2 text-terminal-bg hover:bg-terminal-10"
                    : "bg-terminal-8 text-terminal-fg hover:bg-terminal-7"
                }`}
              >
                {config.matrixRain ? "🟢 Matrix Rain" : "Matrix Rain"}
              </button>
            </div>
          </Tooltip>

          {/* Webcam Flip */}
          <Tooltip text={getOptionHelp(mode, "flip-x")}>
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Webcam
              </label>
              <button
                onClick={() => updateConfig({ flipX: !config.flipX })}
                disabled={disabled}
                className={`w-full px-4 py-2 rounded text-sm font-medium transition-colors ${
                  config.flipX
                    ? "bg-terminal-2 text-terminal-bg hover:bg-terminal-10"
                    : "bg-terminal-8 text-terminal-fg hover:bg-terminal-7"
                }`}
              >
                {config.flipX ? "🟢 Flip Horizontal" : "Flip Horizontal"}
              </button>
            </div>
          </Tooltip>

          {/* Custom Palette Characters (shown when palette is custom) */}
          {config.palette === "custom" && (
            <Tooltip text={getOptionHelp(mode, "palette")}>
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
