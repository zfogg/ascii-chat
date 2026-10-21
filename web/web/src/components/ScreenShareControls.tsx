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
        className="px-4 py-2 bg-terminal-5 text-terminal-bg rounded hover:bg-terminal-13 text-sm font-medium cursor-pointer disabled:opacity-50"
        disabled={disabled || !capture.supported || capture.selecting}
        title={
          capture.supported
            ? "Choose a tab, window, or desktop. Audio is shared when offered by the browser picker."
            : "Screen sharing requires a supported browser and HTTPS or localhost"
        }
        onClick={() => void capture.start()}
      >
        {capture.selecting ? "Choosing screen..." : "Share screen"}
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
    </span>
  );
}
