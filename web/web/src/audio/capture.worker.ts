import { AUDIO_CHANNELS, OPUS_FRAME_SAMPLES } from "./limits";
import {
  createWasmModule,
  type AudioSessionBinding,
  type ClientWasmBindings,
} from "@ascii-chat/shared/wasm";

const send = (message: unknown, transfer: Transferable[] = []) =>
  (self as unknown as Worker).postMessage(message, transfer);

type DspModule = ClientWasmBindings;

self.onmessage = async ({
  data,
}: MessageEvent<{
  port: MessagePort;
  bypass: boolean;
  sampleRate: number;
  encoderChannels: 1 | 2;
  bitrate: number;
}>) => {
  const { port, bypass, sampleRate, encoderChannels, bitrate } = data;
  let module: DspModule | undefined;
  let session: AudioSessionBinding | null = null;
  const fail = (error: unknown) => {
    port.onmessage = null;
    session?.delete();
    session = null;
    send({ error: error instanceof Error ? error.message : String(error) });
  };
  try {
    module = await createWasmModule<DspModule>("client");
    if (!module) throw new Error("Audio WASM did not initialize");
    session = new module.AudioSessionBinding(
      sampleRate,
      bypass,
      encoderChannels,
      bitrate,
      OPUS_FRAME_SAMPLES * AUDIO_CHANNELS,
    );
    if (!session.valid())
      throw new Error("Cannot create browser audio session");
    port.onmessage = ({
      data: frame,
    }: MessageEvent<{ microphone: Float32Array; media: Float32Array }>) => {
      try {
        const input = frame.microphone;
        if (
          input.length !== OPUS_FRAME_SAMPLES * AUDIO_CHANNELS ||
          frame.media.length !== OPUS_FRAME_SAMPLES * AUDIO_CHANNELS
        )
          throw new Error("Invalid microphone frame size");
        const result = session!.process(input, frame.media);
        if (result.code)
          throw new Error(`Audio session processing failed (${result.code})`);
        if (!result.samples || !result.payload)
          throw new Error("Audio session returned an incomplete frame");
        const { samples, payload } = result;
        send({ payload, samples }, [payload.buffer, samples.buffer]);
        port.postMessage(frame, [frame.microphone.buffer, frame.media.buffer]);
      } catch (error) {
        fail(error);
      }
    };
    port.start();
    send({ ready: true });
  } catch (error) {
    fail(error);
  }
};
