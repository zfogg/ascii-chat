import {
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type RefObject,
} from "react";
import type { AsciiRendererHandle } from "@ascii-chat/shared/components/AsciiRenderer";
import type { AudioPipeline } from "../audio/AudioPipeline";
import {
  LocalRecording,
  recordingMimeType,
  type RecordingState,
} from "../recording/LocalRecording";

interface Options {
  rendererRef: RefObject<AsciiRendererHandle | null>;
  ensureAudio: () => AudioPipeline;
  prepare?: () => Promise<boolean>;
  fileElement: () => HTMLMediaElement | null;
  sourceRevision: unknown;
  enabled?: boolean;
}

/** One bounded local export, shared by Mirror and both network modes. */
export function useSessionRecording(options: Options) {
  const latest = useRef(options);
  useLayoutEffect(() => {
    latest.current = options;
  });
  const [state, setState] = useState<RecordingState>("idle");
  const [error, setError] = useState("");
  const [download, setDownload] = useState<{
    url: string;
    name: string;
  } | null>(null);
  const owner = useRef<LocalRecording | null>(null);
  const audio = useRef<AudioPipeline | null>(null);
  const url = useRef("");
  const generation = useRef(0);
  const busy = useRef(false);
  const mounted = useRef(true);

  /** Prepare audio once, cancel stale starts, and retain one completed download. */
  const start = async () => {
    if (busy.current || latest.current.enabled === false) return;
    busy.current = true;
    const request = ++generation.current;
    const current = () =>
      mounted.current &&
      request === generation.current &&
      latest.current.enabled !== false;
    setError("");
    setState("starting");
    try {
      const canvas = latest.current.rendererRef.current?.getCanvas();
      if (!canvas)
        throw new Error("Wait for the ASCII renderer before recording.");
      if (!recordingMimeType() || !canvas.captureStream)
        throw new Error("This browser cannot record ASCII video with audio.");
      if (latest.current.prepare && !(await latest.current.prepare()))
        throw new Error("Connect before recording session audio.");
      if (!current()) return;
      const pipeline = latest.current.ensureAudio();
      audio.current = pipeline;
      const stream = await pipeline.startRecording();
      if (!current()) {
        pipeline.stopRecording();
        return;
      }
      pipeline.setRecordingMedia(latest.current.fileElement());
      const recorder = new LocalRecording(
        (next) => {
          if (mounted.current) setState(next);
        },
        (message) => {
          if (mounted.current) setError(message);
        },
      );
      owner.current = recorder;
      recorder.start(canvas, stream);
      const result = await recorder.completed();
      if (owner.current !== recorder) return;
      pipeline.stopRecording();
      audio.current = null;
      owner.current = null;
      if (result && mounted.current) {
        if (url.current) URL.revokeObjectURL(url.current);
        url.current = URL.createObjectURL(result.blob);
        setDownload({
          url: url.current,
          name: `ascii-chat-${new Date().toISOString().replaceAll(":", "-")}.${result.extension}`,
        });
      }
    } catch (cause) {
      audio.current?.stopRecording();
      audio.current = null;
      owner.current?.dispose();
      owner.current = null;
      busy.current = false;
      if (mounted.current) {
        setError(cause instanceof Error ? cause.message : String(cause));
        setState("idle");
      }
    } finally {
      if (!owner.current) {
        busy.current = false;
        if (mounted.current) setState("idle");
      }
    }
  };

  /** Cancel preparation or finalize the active encoder without ending capture. */
  const stop = async () => {
    if (!busy.current) return;
    if (!owner.current) {
      ++generation.current;
      audio.current?.stopRecording();
      if (mounted.current) setState("finalizing");
      return;
    }
    await owner.current.stop();
  };
  useEffect(() => {
    // Defer the external graph change until all source-switch effects commit.
    let current = true;
    queueMicrotask(() => {
      if (!current) return;
      try {
        audio.current?.setRecordingMedia(latest.current.fileElement());
      } catch (cause) {
        if (mounted.current)
          setError(cause instanceof Error ? cause.message : String(cause));
        void owner.current?.stop();
      }
    });
    return () => {
      current = false;
    };
  }, [options.sourceRevision]);
  useEffect(() => {
    if (options.enabled === false) void owner.current?.stop();
  }, [options.enabled]);
  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      generation.current = generation.current + 1;
      owner.current?.dispose();
      owner.current = null;
      audio.current?.stopRecording();
      audio.current = null;
      busy.current = false;
      if (url.current) URL.revokeObjectURL(url.current);
      url.current = "";
    };
  }, []);
  return {
    state,
    error,
    download,
    start,
    stop,
    supported:
      recordingMimeType() !== null &&
      typeof HTMLCanvasElement.prototype.captureStream === "function",
  };
}
