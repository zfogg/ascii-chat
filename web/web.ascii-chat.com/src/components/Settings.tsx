import { Heading } from "@ascii-chat/shared/components";

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
export type Resolution = "320x240" | "640x480" | "1280x720" | "1920x1080";

export interface SettingsConfig {
  resolution: Resolution;
  targetFps: number;
  colorMode: ColorMode;
  colorFilter: ColorFilter;
  palette: Palette;
  paletteChars?: string;
  matrixRain?: boolean;
  webcamFlip?: boolean;
}

interface SettingsProps {
  config: SettingsConfig;
  onChange: (config: SettingsConfig) => void;
  disabled?: boolean;
}

export function Settings({
  config,
  onChange,
  disabled = false,
}: SettingsProps) {
  const updateConfig = (updates: Partial<SettingsConfig>) => {
    onChange({ ...config, ...updates });
  };

  return (
    <div className="border-b border-terminal-8 bg-terminal-0">
      <div className="px-4 py-3">
        <Heading
          level={3}
          className="text-sm font-semibold text-terminal-fg mb-3"
        >
          Settings
        </Heading>
        <div className="flex flex-wrap gap-3">
          {/* Resolution */}
          <div className="flex-1 min-w-[200px]">
            <label className="block text-xs font-medium text-terminal-8 mb-1">
              Resolution
            </label>
            <select
              value={config.resolution}
              onChange={(e) =>
                updateConfig({ resolution: e.target.value as Resolution })
              }
              disabled={disabled}
              className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4"
            >
              <option value="320x240">320×240 (QVGA)</option>
              <option value="640x480">640×480 (VGA)</option>
              <option value="1280x720">1280×720 (HD)</option>
              <option value="1920x1080">1920×1080 (Full HD)</option>
            </select>
          </div>

          {/* Frame Rate */}
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

          {/* Color Mode */}
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

          {/* Color Filter */}
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

          {/* Palette */}
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

          {/* Matrix Rain Effect */}
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

          {/* Webcam Flip */}
          <div className="flex-1 min-w-[200px]">
            <label className="block text-xs font-medium text-terminal-8 mb-1">
              Webcam
            </label>
            <button
              onClick={() => updateConfig({ webcamFlip: !config.webcamFlip })}
              disabled={disabled}
              className={`w-full px-4 py-2 rounded text-sm font-medium transition-colors ${
                config.webcamFlip
                  ? "bg-terminal-2 text-terminal-bg hover:bg-terminal-10"
                  : "bg-terminal-8 text-terminal-fg hover:bg-terminal-7"
              }`}
            >
              {config.webcamFlip ? "🟢 Flip Horizontal" : "Flip Horizontal"}
            </button>
          </div>

          {/* Custom Palette Characters (shown when palette is custom) */}
          {config.palette === "custom" && (
            <div className="flex-1 min-w-[200px]">
              <label className="block text-xs font-medium text-terminal-8 mb-1">
                Custom Characters (dark → bright)
              </label>
              <input
                type="text"
                value={config.paletteChars || " =#░░▒▒▓▓██"}
                onChange={(e) => updateConfig({ paletteChars: e.target.value })}
                disabled={disabled}
                placeholder=" =#░░▒▒▓▓██"
                className="w-full px-2 py-1 bg-terminal-bg border border-terminal-8 rounded text-sm text-terminal-fg focus:outline-none focus:border-terminal-4 font-mono"
              />
              <div className="text-xs text-terminal-8 mt-1">
                Enter characters from darkest to brightest
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
