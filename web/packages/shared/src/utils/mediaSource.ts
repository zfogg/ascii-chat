export const MediaSourceType = {
  WEBCAM: Symbol("webcam"),
  FILE: Symbol("file"),
  MICROPHONE: Symbol("microphone"),
} as const;

export type MediaSource =
  | (typeof MediaSourceType)[keyof typeof MediaSourceType]
  | null;
