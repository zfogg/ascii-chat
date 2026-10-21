export interface AudioCaptureOptions {
  bypass: boolean;
  onFrame: (payload: Uint8Array, samples: Float32Array) => void;
  onError: (error: Error) => void;
}

/** Owns the silent-output worklet and C/WASM worker for one capture input graph. */
export class AudioCapture {
  private constructor(
    public readonly node: AudioWorkletNode,
    private readonly worker: Worker,
  ) {}

  static async create(
    context: AudioContext,
    options: AudioCaptureOptions,
  ): Promise<AudioCapture> {
    if (context.sampleRate !== 48000)
      throw new Error("Audio capture requires a 48 kHz AudioContext");
    if (!context.audioWorklet)
      throw new Error(
        "This browser does not support AudioWorklet microphone capture",
      );
    await context.audioWorklet.addModule(
      new URL("./capture-worklet.js", import.meta.url),
    );
    const node = new AudioWorkletNode(context, "ascii-chat-pcm-capture", {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      outputChannelCount: [1],
    });
    let worker: Worker;
    try {
      worker = new Worker(new URL("./capture.worker.ts", import.meta.url), {
        type: "module",
      });
    } catch (error) {
      node.disconnect();
      node.port.close();
      throw error;
    }
    const capture = new AudioCapture(node, worker);
    try {
      await new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(
          () => reject(new Error("Audio processor startup timed out")),
          30000,
        );
        const fail = (error: Error) => {
          clearTimeout(timeout);
          reject(error);
        };
        worker.onerror = (event) =>
          fail(new Error(event.message || "Audio processor worker failed"));
        worker.onmessage = ({ data }) => {
          if (data.error) {
            fail(new Error(data.error));
            return;
          }
          if (data.ready) {
            clearTimeout(timeout);
            resolve();
          }
        };
        const channel = new MessageChannel();
        node.port.postMessage({ port: channel.port1 }, [channel.port1]);
        worker.postMessage(
          {
            port: channel.port2,
            bypass: options.bypass,
            sampleRate: context.sampleRate,
          },
          [channel.port2],
        );
      });
      worker.onerror = (event) =>
        options.onError(
          new Error(event.message || "Audio processor worker failed"),
        );
      worker.onmessage = ({ data }) => {
        if (data.error) options.onError(new Error(data.error));
        else if (data.payload) options.onFrame(data.payload, data.samples);
      };
      node.onprocessorerror = () =>
        options.onError(new Error("Microphone capture worklet failed"));
      // The capture worklet emits silence, keeping it scheduled without microphone monitoring.
      node.connect(context.destination);
      return capture;
    } catch (error) {
      capture.close();
      throw error;
    }
  }

  close(): void {
    this.node.onprocessorerror = null;
    this.node.disconnect();
    this.node.port.close();
    this.worker.onmessage = this.worker.onerror = null;
    this.worker.terminate();
  }
}
