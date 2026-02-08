/**
 * Web Audio API integration for ascii-chat
 * Handles audio capture, playback, and Opus codec integration
 */

export interface AudioPipelineOptions {
  sampleRate?: number;
  channelCount?: number;
  echoCancellation?: boolean;
  noiseSuppression?: boolean;
  onAudioData?: (opusData: Uint8Array) => void;
}

export class AudioPipeline {
  private audioContext: AudioContext | null = null;
  private mediaStream: MediaStream | null = null;
  private sourceNode: MediaStreamAudioSourceNode | null = null;
  private processorNode: ScriptProcessorNode | null = null;
  private onAudioDataCallback: ((opusData: Uint8Array) => void) | null = null;

  constructor(private options: AudioPipelineOptions = {}) {
    this.onAudioDataCallback = options.onAudioData || null;
  }

  /**
   * Start audio capture from microphone
   */
  async startCapture(): Promise<void> {
    console.log('[AudioPipeline] Starting audio capture...');

    // Request microphone access
    const stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        sampleRate: this.options.sampleRate || 48000,
        channelCount: this.options.channelCount || 1,
        echoCancellation: this.options.echoCancellation ?? true,
        noiseSuppression: this.options.noiseSuppression ?? true,
        autoGainControl: true
      }
    });

    this.mediaStream = stream;

    // Create audio context
    this.audioContext = new AudioContext({
      sampleRate: this.options.sampleRate || 48000
    });

    // Create source node from media stream
    this.sourceNode = this.audioContext.createMediaStreamSource(stream);

    // Create script processor for audio data extraction
    // Buffer size of 4096 samples
    this.processorNode = this.audioContext.createScriptProcessor(4096, 1, 1);

    this.processorNode.onaudioprocess = (event) => {
      const inputBuffer = event.inputBuffer;
      const inputData = inputBuffer.getChannelData(0); // Mono audio

      // Convert Float32Array to Int16Array (PCM)
      const pcmData = new Int16Array(inputData.length);
      for (let i = 0; i < inputData.length; i++) {
        const sample = Math.max(-1, Math.min(1, inputData[i]));
        pcmData[i] = sample < 0 ? sample * 0x8000 : sample * 0x7FFF;
      }

      // TODO: Encode with Opus WASM codec
      // For now, just pass raw PCM data
      // In production, this would call OpusEncoder.encode()
      const rawBytes = new Uint8Array(pcmData.buffer);
      this.onAudioDataCallback?.(rawBytes);
    };

    // Connect nodes
    this.sourceNode.connect(this.processorNode);
    this.processorNode.connect(this.audioContext.destination);

    console.log('[AudioPipeline] Audio capture started');
  }

  /**
   * Play received audio data
   */
  async playAudioData(opusData: Uint8Array): Promise<void> {
    if (!this.audioContext) {
      this.audioContext = new AudioContext({
        sampleRate: this.options.sampleRate || 48000
      });
    }

    try {
      // TODO: Decode Opus data using WASM Opus codec
      // For now, this is a placeholder
      // In production, this would call OpusEncoder.decode()

      // Placeholder: assume raw PCM data for now
      const pcmData = new Int16Array(opusData.buffer);

      // Convert Int16 PCM to Float32
      const floatData = new Float32Array(pcmData.length);
      for (let i = 0; i < pcmData.length; i++) {
        floatData[i] = pcmData[i] / (pcmData[i] < 0 ? 0x8000 : 0x7FFF);
      }

      // Create audio buffer
      const audioBuffer = this.audioContext.createBuffer(
        1, // Mono
        floatData.length,
        this.audioContext.sampleRate
      );

      // Copy data to buffer
      audioBuffer.getChannelData(0).set(floatData);

      // Create buffer source and play
      const source = this.audioContext.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(this.audioContext.destination);
      source.start();
    } catch (error) {
      console.error('[AudioPipeline] Failed to play audio:', error);
    }
  }

  /**
   * Set audio data callback
   */
  onAudioData(callback: (opusData: Uint8Array) => void): void {
    this.onAudioDataCallback = callback;
  }

  /**
   * Stop audio capture
   */
  stopCapture(): void {
    console.log('[AudioPipeline] Stopping audio capture...');

    if (this.processorNode) {
      this.processorNode.disconnect();
      this.processorNode = null;
    }

    if (this.sourceNode) {
      this.sourceNode.disconnect();
      this.sourceNode = null;
    }

    if (this.mediaStream) {
      this.mediaStream.getTracks().forEach(track => track.stop());
      this.mediaStream = null;
    }

    if (this.audioContext) {
      this.audioContext.close();
      this.audioContext = null;
    }

    console.log('[AudioPipeline] Audio capture stopped');
  }

  /**
   * Check if audio is currently being captured
   */
  isCapturing(): boolean {
    return this.mediaStream !== null && this.audioContext !== null;
  }
}
