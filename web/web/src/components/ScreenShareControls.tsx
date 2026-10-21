interface Props {
  capture: {
    active: boolean;
    selecting: boolean;
    supported: boolean;
    start: () => Promise<void>;
    stop: () => void;
    release: () => void;
  };
  disabled?: boolean;
}

export function ScreenShareControls({ capture, disabled = false }: Props) {
  return (
    <span className="flex flex-wrap items-center gap-2">
      <button
        type="button"
        className="border border-terminal-8 rounded px-3 py-1 cursor-pointer disabled:opacity-50"
        disabled={disabled || !capture.supported || capture.selecting}
        title={
          capture.supported
            ? "Choose a tab, window, or desktop. Screen audio is not captured; microphone controls are unchanged."
            : "Screen sharing requires a supported browser and HTTPS or localhost"
        }
        onClick={() => void capture.start()}
      >
        {capture.selecting
          ? "Choosing screen..."
          : capture.active
            ? "Change shared screen"
            : "Share screen"}
      </button>
      {(capture.active || capture.selecting) && (
        <button
          type="button"
          className="border border-red-700 rounded px-3 py-1"
          onClick={capture.active ? capture.stop : capture.release}
        >
          {capture.active ? "Stop sharing" : "Cancel screen selection"}
        </button>
      )}
      {capture.active && (
        <span role="status" className="text-xs">
          Sharing screen · video only
        </span>
      )}
    </span>
  );
}
