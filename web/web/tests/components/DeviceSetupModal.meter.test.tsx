import "@testing-library/jest-dom/vitest";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { DeviceSetupModal } from "../../src/components/DeviceSetupModal";

vi.mock("../../src/hooks/useVideoEncodings", () => ({
  useVideoEncodings: () => ({
    loading: false,
    supportedEncodings: ["raw"],
    selectedEncoding: "raw",
    bestEncoding: "raw",
    selectEncoding: vi.fn(),
  }),
  VIDEO_ENCODING_LABELS: { raw: "Raw pixels" },
}));

function device(
  deviceId: string,
  kind: MediaDeviceKind,
  label: string,
): MediaDeviceInfo {
  return { deviceId, kind, label, groupId: deviceId, toJSON: () => ({}) };
}

class Track {
  stop = vi.fn();
  constructor(
    public kind: string,
    public id: string,
  ) {}
  getSettings() {
    return { deviceId: this.id };
  }
}

class Stream {
  constructor(private tracks: Track[] = []) {}
  getTracks() {
    return [...this.tracks];
  }
  getVideoTracks() {
    return this.tracks.filter((track) => track.kind === "video");
  }
  getAudioTracks() {
    return this.tracks.filter((track) => track.kind === "audio");
  }
  addTrack(track: Track) {
    this.tracks.push(track);
  }
  removeTrack(track: Track) {
    this.tracks = this.tracks.filter((candidate) => candidate !== track);
  }
}

const source = { connect: vi.fn(), disconnect: vi.fn() };
const resume = vi.fn().mockResolvedValue(undefined);
const closeAudio = vi.fn().mockResolvedValue(undefined);
const getUserMedia = vi.fn();
const enumerateDevices = vi.fn();
let sampleAmplitude = 0;
let initialAudioState: AudioContextState = "running";
let audioContexts: Array<EventTarget & { state: AudioContextState }>;
let mediaDevices: EventTarget;
let tracks: Track[];

beforeEach(() => {
  window.localStorage.clear();
  window.history.replaceState({}, "", "/client?devices=true");
  vi.clearAllMocks();
  resume.mockResolvedValue(undefined);
  sampleAmplitude = 0;
  initialAudioState = "running";
  audioContexts = [];
  tracks = [];
  mediaDevices = Object.assign(new EventTarget(), {
    getUserMedia,
    enumerateDevices,
  });
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: mediaDevices,
  });
  enumerateDevices.mockResolvedValue([
    device("camera-busy", "videoinput", "Busy webcam"),
    device("camera-free", "videoinput", "Other webcam"),
    device("mic-one", "audioinput", "Desk microphone"),
    device("mic-two", "audioinput", "Headset microphone"),
    device("default", "audiooutput", "System speakers"),
  ]);
  getUserMedia.mockImplementation(async () => {
    const camera = new Track("video", "camera-free");
    const microphone = new Track("audio", "mic-one");
    tracks.push(camera, microphone);
    return new Stream([camera, microphone]);
  });
  vi.stubGlobal("MediaStream", Stream);
  vi.stubGlobal(
    "AudioContext",
    class extends EventTarget {
      state = initialAudioState;
      constructor() {
        super();
        audioContexts.push(this);
      }
      resume = resume;
      close = closeAudio;
      createMediaStreamSource = vi.fn(() => source);
      createAnalyser = vi.fn(() => ({
        fftSize: 512,
        getFloatTimeDomainData: (samples: Float32Array) =>
          samples.fill(sampleAmplitude),
      }));
    },
  );
  vi.stubGlobal(
    "requestAnimationFrame",
    vi.fn(() => 1),
  );
  vi.stubGlobal("cancelAnimationFrame", vi.fn());
  vi.spyOn(HTMLMediaElement.prototype, "play").mockImplementation(function (
    this: HTMLMediaElement,
  ) {
    this.dispatchEvent(new Event("loadeddata"));
    return Promise.resolve();
  });
  vi.spyOn(HTMLMediaElement.prototype, "pause").mockImplementation(
    () => undefined,
  );
});

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

describe("Device setup microphone levels", () => {
  it("meters quiet input automatically and keeps the displayed and accessible levels in sync", async () => {
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await screen.findByRole("button", { name: "Stop test" });
    const meter = screen.getByRole("meter", { name: "Microphone input level" });
    const readNextLevel = () => {
      const callback = vi.mocked(requestAnimationFrame).mock.calls.at(-1)![0];
      act(() => callback(0));
    };

    sampleAmplitude = 0.005;
    readNextLevel();
    const quietLevel = Number(meter.getAttribute("aria-valuenow"));
    expect(quietLevel).toBeGreaterThan(20);
    expect(quietLevel).toBeLessThan(25);
    expect(
      parseFloat((meter.firstChild as HTMLElement).style.width),
    ).toBeGreaterThan(20);

    sampleAmplitude = 0.1;
    readNextLevel();
    expect(Number(meter.getAttribute("aria-valuenow"))).toBeGreaterThan(
      quietLevel,
    );

    sampleAmplitude = 0;
    readNextLevel();
    expect(meter).toHaveAttribute("aria-valuenow", "0");
    expect(meter.firstChild).toHaveStyle({ width: "0%" });

    fireEvent.click(screen.getByRole("button", { name: "Stop test" }));
    sampleAmplitude = 0.1;
    readNextLevel();
    expect(meter).toHaveAttribute("aria-valuenow", "0");
    expect(meter.firstChild).toHaveStyle({ width: "0%" });
    fireEvent.click(screen.getByRole("button", { name: "Test microphone" }));
    readNextLevel();
    expect(Number(meter.getAttribute("aria-valuenow"))).toBeGreaterThan(
      quietLevel,
    );
  });

  it("allows a click to resume metering when browser autoplay initially suspends the context", async () => {
    initialAudioState = "suspended";
    resume.mockImplementationOnce(() => new Promise(() => {}));
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    const start = await screen.findByRole("button", {
      name: "Test microphone",
    });
    await waitFor(() => expect(start).toBeEnabled());
    const meter = screen.getByRole("meter");
    expect(meter).toHaveAttribute("aria-valuenow", "0");
    expect(
      screen.getByText("Click Test microphone to enable microphone levels."),
    ).toBeVisible();

    resume.mockImplementationOnce(async () => {
      audioContexts[0]!.state = "running";
      audioContexts[0]!.dispatchEvent(new Event("statechange"));
    });
    fireEvent.click(start);
    await screen.findByRole("button", { name: "Stop test" });
    sampleAmplitude = 0.05;
    const callback = vi.mocked(requestAnimationFrame).mock.calls.at(-1)![0];
    act(() => callback(0));
    expect(Number(meter.getAttribute("aria-valuenow"))).toBeGreaterThan(50);
  });
});
