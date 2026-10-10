import { useCallback, useEffect, useState } from "react";
import {
  VideoUploadEncoder,
  type CompressedVideoEncoding,
  type VideoChunk,
} from "../network/VideoUploadEncoder";
import {
  getVideoEncoding,
  selectVideoEncoder,
  type VideoEncoding,
} from "../network/videoEncoding";
import { writeUrlValue } from "../utils/urlState";

export const VIDEO_ENCODING_STORAGE_KEY = "ascii-chat.video-encoding";
export const VIDEO_ENCODING_CHANGED = "ascii-chat:video-encoding-changed";
export type AvailableVideoEncoding = CompressedVideoEncoding | "raw";
export const VIDEO_ENCODING_LABELS: Record<AvailableVideoEncoding, string> = {
  hevc: "HEVC (H.265)",
  h264: "H.264",
  raw: "Raw pixels",
};

function readPreference(): {
  preference: VideoEncoding;
  source: "url" | "saved" | "automatic";
  error: string | null;
} {
  if (new URLSearchParams(window.location.search).has("encoding")) {
    try {
      return { preference: getVideoEncoding(), source: "url", error: null };
    } catch (error) {
      return { preference: "auto", source: "url", error: String(error) };
    }
  }
  try {
    const saved = window.localStorage.getItem(VIDEO_ENCODING_STORAGE_KEY);
    if (saved)
      return {
        preference: getVideoEncoding(`?encoding=${encodeURIComponent(saved)}`),
        source: "saved",
        error: null,
      };
  } catch {
    /* Storage can be unavailable or contain an obsolete value. */
  }
  return { preference: "auto", source: "automatic", error: null };
}

/** Shared capability probing and URL > saved > automatic selection policy. */
export function useVideoEncodings({
  width = 320,
  height = 240,
  fps = 60,
} = {}) {
  const [choice, setChoice] = useState(readPreference);
  const [supportedEncodings, setSupportedEncodings] = useState<
    AvailableVideoEncoding[]
  >(["raw"]);
  const [loading, setLoading] = useState(true);
  useEffect(() => {
    const refresh = () => setChoice(readPreference());
    window.addEventListener(VIDEO_ENCODING_CHANGED, refresh);
    window.addEventListener("popstate", refresh);
    window.addEventListener("storage", refresh);
    return () => {
      window.removeEventListener(VIDEO_ENCODING_CHANGED, refresh);
      window.removeEventListener("popstate", refresh);
      window.removeEventListener("storage", refresh);
    };
  }, []);
  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    void Promise.all(
      (["hevc", "h264"] as const).map(async (codec) => ({
        codec,
        supported: await VideoUploadEncoder.supports(
          VideoUploadEncoder.configuration(codec, width, height, fps),
        ),
      })),
    ).then((results) => {
      if (cancelled) return;
      setSupportedEncodings([
        ...results
          .filter((result) => result.supported)
          .map((result) => result.codec),
        "raw",
      ]);
      setLoading(false);
    });
    return () => {
      cancelled = true;
    };
  }, [width, height, fps]);
  const bestEncoding = supportedEncodings[0]!;
  const selectedEncoding =
    choice.preference === "auto" ? bestEncoding : choice.preference;
  const validate = useCallback(() => {
    if (choice.error) throw new Error(choice.error);
  }, [choice.error]);
  // Only the modal calls this explicit setter. Probing/URL restoration never writes storage.
  const selectEncoding = useCallback(
    (encoding: VideoEncoding) => {
      if (encoding !== "auto" && !supportedEncodings.includes(encoding))
        throw new Error(
          `${VIDEO_ENCODING_LABELS[encoding]} encoding is unsupported. Use ?encoding=raw.`,
        );
      window.localStorage.setItem(VIDEO_ENCODING_STORAGE_KEY, encoding);
      writeUrlValue("encoding", encoding === "h264" ? "H.264" : encoding);
      window.dispatchEvent(new Event(VIDEO_ENCODING_CHANGED));
    },
    [supportedEncodings],
  );
  const createEncoder = useCallback(
    (
      actualWidth: number,
      actualHeight: number,
      actualFps: number,
      output: (chunk: VideoChunk, codec: CompressedVideoEncoding) => void,
      after?: CompressedVideoEncoding,
    ) => {
      validate();
      return selectVideoEncoder(
        choice.preference,
        actualWidth,
        actualHeight,
        actualFps,
        output,
        after,
      );
    },
    [choice.preference, validate],
  );
  return {
    ...choice,
    supportedEncodings,
    bestEncoding,
    selectedEncoding,
    loading,
    selectEncoding,
    createEncoder,
    validate,
  };
}
