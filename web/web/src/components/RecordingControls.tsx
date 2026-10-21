import type { useSessionRecording } from "../hooks/useSessionRecording";
import {
  RECORDING_LIMIT_BYTES,
  RECORDING_LIMIT_MS,
} from "../recording/LocalRecording";

/** Keep completed downloads and errors reachable even when starting is disabled. */
export function RecordingControls({
  recording,
  disabled = false,
}: {
  recording: ReturnType<typeof useSessionRecording>;
  disabled?: boolean;
}) {
  const active = recording.state !== "idle";
  return (
    <div className="recording-controls text-xs" aria-label="Local recording">
      <button
        type="button"
        title="Save the displayed ASCII with enabled microphone, current file/screen audio and received peers. Speaker monitoring is independent. Recordings stay on this device."
        className="recording-button border rounded px-3 py-2 border-red-600"
        disabled={
          recording.state === "finalizing" ||
          (!active && (disabled || !recording.supported))
        }
        onClick={() => void (active ? recording.stop() : recording.start())}
      >
        {recording.state === "finalizing"
          ? "Finishing recording…"
          : active
            ? "Stop recording"
            : "Record ASCII"}
      </button>
      <div className="recording-details">
        <div className="recording-message">
          {recording.state === "recording" && !recording.error && (
            <span role="status">
              Recording locally ({RECORDING_LIMIT_MS / 60_000} min /{" "}
              {RECORDING_LIMIT_BYTES / (1024 * 1024)} MiB limit)
            </span>
          )}
          {!recording.supported && (
            <span role="status">
              ASCII recording is unsupported in this browser.
            </span>
          )}
          {recording.error && <span role="alert">{recording.error}</span>}
        </div>
        <div className="recording-download">
          {recording.download ? (
            <a
              className="underline"
              href={recording.download.url}
              download={recording.download.name}
            >
              Download recording
            </a>
          ) : (
            <span aria-hidden="true" className="invisible">
              Download recording
            </span>
          )}
        </div>
      </div>
    </div>
  );
}
