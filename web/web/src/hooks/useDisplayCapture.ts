import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";

interface Options {
  onStart: (stream: MediaStream) => Promise<unknown>;
  onStop: () => void;
  setError: (message: string) => void;
}

/** Own only display tracks. Microphones belong to the existing media pipeline. */
export function useDisplayCapture(options: Options) {
  const callbacks = useRef(options);
  useLayoutEffect(() => {
    callbacks.current = options;
  });
  const stream = useRef<MediaStream | null>(null);
  const generation = useRef(0);
  const mounted = useRef(true);
  const pending = useRef(false);
  const [active, setActive] = useState(false);
  const [revision, setRevision] = useState(0);
  const [selecting, setSelecting] = useState(false);
  const supported =
    typeof navigator.mediaDevices?.getDisplayMedia === "function";

  const release = useCallback(() => {
    generation.current++;
    pending.current = false;
    const previous = stream.current;
    stream.current = null;
    // A pipeline may have added its microphone to this stream. Never own it.
    previous?.getVideoTracks().forEach((track) => track.stop());
    if (mounted.current) {
      setActive(false);
      setSelecting(false);
    }
  }, []);
  const stop = useCallback(() => {
    const wasActive = !!stream.current;
    release();
    if (wasActive) callbacks.current.onStop();
  }, [release]);

  const start = useCallback(async () => {
    if (pending.current) return;
    if (!supported) {
      callbacks.current.setError(
        "Screen sharing is unavailable in this browser. Use a supported browser over HTTPS or localhost.",
      );
      return;
    }
    const request = ++generation.current;
    pending.current = true;
    setSelecting(true);
    callbacks.current.setError("");
    let selected: MediaStream | undefined;
    try {
      // Call synchronously from the click to preserve transient user activation.
      selected = await navigator.mediaDevices.getDisplayMedia({
        video: true,
        audio: false,
      });
      if (request !== generation.current || !mounted.current) {
        selected.getTracks().forEach((track) => track.stop());
        return;
      }
      const video = selected.getVideoTracks()[0];
      if (!video || video.readyState !== "live")
        throw new Error("No live display video was selected");
      // Defensive cleanup if a browser returns audio despite audio:false.
      selected.getAudioTracks().forEach((track) => {
        track.stop();
        selected!.removeTrack(track);
      });
      const previous = stream.current;
      stream.current = selected;
      video.addEventListener(
        "ended",
        () => {
          if (stream.current === selected) stop();
        },
        { once: true },
      );
      let started: unknown;
      try {
        started = await callbacks.current.onStart(selected);
      } finally {
        previous?.getVideoTracks().forEach((track) => track.stop());
      }
      if (request !== generation.current || !mounted.current) {
        selected.getVideoTracks().forEach((track) => track.stop());
        return;
      }
      if (started === false || video.readyState !== "live")
        throw new Error("Display capture could not start");
      setRevision((value) => value + 1);
      setActive(true);
    } catch (error) {
      if (request !== generation.current || !mounted.current) return;
      if (selected && stream.current === selected) {
        release();
        callbacks.current.onStop();
      } else selected?.getTracks().forEach((track) => track.stop());
      if (
        !(
          error instanceof DOMException &&
          (error.name === "NotAllowedError" || error.name === "AbortError")
        )
      )
        callbacks.current.setError(`Failed to share screen: ${String(error)}`);
    } finally {
      if (request === generation.current && mounted.current) {
        pending.current = false;
        setSelecting(false);
      }
    }
  }, [supported, stop, release]);

  useEffect(() => {
    mounted.current = true;
    return () => {
      mounted.current = false;
      release();
    };
  }, [release]);
  return { active, revision, selecting, supported, start, stop, release };
}
