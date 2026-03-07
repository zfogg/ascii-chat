interface PageControlBarProps {
  title?: string | undefined;
  status?: string | undefined;
  statusDotColor?: string | undefined;
  dimensions?: { cols: number; rows: number } | undefined;
  fps?: number | undefined;
  targetFps?: number | undefined;
  isWebcamRunning?: boolean | undefined;
  onStartWebcam?: (() => void) | undefined;
  onStopWebcam?: (() => void) | undefined;
  showConnectionButton?: boolean | undefined;
  onConnectionClick?: (() => void) | undefined;
  onSettingsClick?: (() => void) | undefined;
  showSettingsButton?: boolean | undefined;
}

export function PageControlBar({
  title,
  status,
  statusDotColor,
  dimensions,
  fps,
  targetFps,
  isWebcamRunning = false,
  onStartWebcam,
  onStopWebcam,
  showConnectionButton = false,
  onConnectionClick,
  onSettingsClick,
  showSettingsButton = true,
}: PageControlBarProps) {
  const getFpsColor = () => {
    if (fps === undefined || fps === null) return "text-terminal-8";
    if (targetFps === undefined) return "text-terminal-2"; // default green

    const difference = targetFps - fps;
    if (difference >= 10) return "text-terminal-1"; // red if 10+ fps slower
    return "text-terminal-2"; // green if near target
  };

  const showWebcamButton = onStartWebcam || onStopWebcam;

  return (
    <div className="px-4 py-3 flex-shrink-0 border-b border-terminal-8">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          {statusDotColor && (
            <div className="flex items-center gap-2">
              <div className={`w-2 h-2 rounded-full ${statusDotColor}`} />
              {title && <span className="text-sm font-semibold">{title}</span>}
            </div>
          )}
          {!statusDotColor && title && (
            <h2 className="text-sm font-semibold">{title}</h2>
          )}
          {dimensions && dimensions.cols > 0 && (
            <span className="text-xs text-terminal-8">
              {dimensions.cols}x{dimensions.rows}
            </span>
          )}
          {status && (
            <span className="status text-xs text-terminal-8">{status}</span>
          )}
          {fps !== undefined && (
            <span className={`text-xs ${getFpsColor()}`}>
              FPS: {fps}
              {targetFps && ` / ${targetFps}`}
            </span>
          )}
        </div>
        <div className="flex gap-2">
          {showWebcamButton &&
            (isWebcamRunning ? (
              <button
                onClick={onStopWebcam}
                className="px-4 py-2 bg-terminal-1 text-terminal-bg rounded hover:bg-terminal-9 text-sm font-medium"
              >
                Stop Webcam
              </button>
            ) : (
              <button
                onClick={onStartWebcam}
                className="px-4 py-2 bg-terminal-2 text-terminal-bg rounded hover:bg-terminal-10 text-sm font-medium"
              >
                Start Webcam
              </button>
            ))}
          {showConnectionButton && (
            <button
              onClick={onConnectionClick}
              className="px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
            >
              Connection
            </button>
          )}
          {showSettingsButton && (
            <button
              onClick={onSettingsClick}
              className="px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
            >
              Settings
            </button>
          )}
        </div>
      </div>
    </div>
  );
}
