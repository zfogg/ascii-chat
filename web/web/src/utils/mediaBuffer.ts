/** Reserve enough media for a converter that is slower than realtime to finish. */
export function startupBuffer(
  duration: number,
  converted: number,
  elapsed: number,
) {
  const rate = elapsed > 0 ? converted / elapsed : 0;
  // Leave headroom for scheduling jitter and fluctuations in scene complexity.
  const safeRate = rate * 0.75;
  return Math.min(duration, Math.max(3, duration * (1 - safeRate) + 3));
}

export function bufferedAhead(ranges: TimeRanges, time: number) {
  for (let i = 0; i < ranges.length; i++) {
    if (ranges.start(i) <= time + 0.05 && ranges.end(i) > time)
      return ranges.end(i) - time;
  }
  return 0;
}

export interface MediaProbe {
  format?: { duration?: string };
  streams?: { codec_type?: string; codec_name?: string; profile?: string }[];
}

export function nativeAudioSupported(info: MediaProbe) {
  const audio = info.streams?.find((stream) => stream.codec_type === "audio");
  const types: Record<string, string> = {
    opus: 'audio/webm; codecs="opus"',
    vorbis: 'audio/webm; codecs="vorbis"',
    mp3: "audio/mpeg",
  };
  const type =
    audio?.codec_name === "aac" && audio.profile === "LC"
      ? 'audio/mp4; codecs="mp4a.40.2"'
      : types[audio?.codec_name ?? ""];
  return (
    !!info.streams &&
    (!audio ||
      (!!type &&
        document.createElement("audio").canPlayType(type) === "probably"))
  );
}
