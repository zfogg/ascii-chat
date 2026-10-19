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

function stream(kind: string, id: string) {
  const track = new Track(kind, id);
  tracks.push(track);
  return new Stream([track]);
}

function requestedDevice(constraints: MediaStreamConstraints) {
  const input = constraints.video || constraints.audio;
  return ((input as MediaTrackConstraints)?.deviceId ?? {
    exact: constraints.video ? "camera-busy" : "mic-one",
  }) as ConstrainDOMStringParameters;
}

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
  getUserMedia.mockImplementation(
    async (constraints: MediaStreamConstraints) => {
      const id = String(requestedDevice(constraints).exact);
      if (id === "camera-busy")
        throw new DOMException("Device in use", "NotReadableError");
      return stream(constraints.video ? "video" : "audio", id);
    },
  );
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

describe("Device setup with unavailable inputs", () => {
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

  it("releases a camera whose video never starts and restores an actionable retry", async () => {
    vi.useFakeTimers();
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) =>
        stream(
          constraints.video ? "video" : "audio",
          String(requestedDevice(constraints).exact),
        ),
    );
    vi.mocked(HTMLMediaElement.prototype.play).mockImplementation(function (
      this: HTMLMediaElement,
    ) {
      this.dispatchEvent(new Event("loadeddata"));
      return new Promise(() => {});
    });
    await act(async () => {
      render(<DeviceSetupModal open onClose={vi.fn()} />);
    });
    const camera = tracks.find((track) => track.kind === "video")!;
    const microphone = tracks.find((track) => track.kind === "audio")!;
    expect(screen.getByText("Starting camera preview…")).toBeInTheDocument();
    await act(async () => {
      await vi.advanceTimersByTimeAsync(10_000);
    });
    expect(screen.getByRole("button", { name: "Retry camera" })).toBeEnabled();
    expect(screen.getByRole("status")).toHaveTextContent("Startup timed out");
    expect(
      screen.queryByText("Starting camera preview…"),
    ).not.toBeInTheDocument();
    expect(
      (screen.getByLabelText("Camera preview") as HTMLVideoElement).srcObject,
    ).toBeNull();
    expect(camera.stop).toHaveBeenCalledOnce();
    expect(microphone.stop).not.toHaveBeenCalled();
    expect(
      screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
    ).toBeEnabled();
  });

  it.each([
    {
      label: "Camera",
      kind: "video",
      failed: "camera-busy",
      other: "camera-free",
      retry: "Retry camera",
      unaffectedKind: "audio",
    },
    {
      label: "Microphone",
      kind: "audio",
      failed: "mic-one",
      other: "mic-two",
      retry: "Retry microphone",
      unaffectedKind: "video",
    },
  ])(
    "reselecting a failed $label retries it and leaves its Retry button available after another failure",
    async ({ label, kind, failed, other, retry, unaffectedKind }) => {
      getUserMedia.mockImplementation(
        async (constraints: MediaStreamConstraints) => {
          const id = String(requestedDevice(constraints).exact);
          if (id === failed)
            throw new DOMException("Device in use", "NotReadableError");
          return stream(constraints.video ? "video" : "audio", id);
        },
      );
      render(<DeviceSetupModal open onClose={vi.fn()} />);
      await screen.findByRole("button", { name: retry });
      await waitFor(() =>
        expect(
          tracks.find((track) => track.kind === unaffectedKind),
        ).toBeDefined(),
      );
      const unaffected = tracks.find((track) => track.kind === unaffectedKind)!;
      const input = screen.getByLabelText(label, { exact: true });
      const attempts = () =>
        getUserMedia.mock.calls.filter(([constraints]) =>
          kind === "video" ? constraints.video : constraints.audio,
        );
      fireEvent.change(input, { target: { value: other } });
      await waitFor(() => expect(attempts()).toHaveLength(2));
      await waitFor(() =>
        expect(
          screen.queryByRole("button", { name: retry }),
        ).not.toBeInTheDocument(),
      );
      fireEvent.change(input, { target: { value: failed } });
      expect(await screen.findByRole("button", { name: retry })).toBeEnabled();
      expect(attempts()).toHaveLength(3);
      expect(input).toHaveValue(failed);
      expect(unaffected.stop).not.toHaveBeenCalled();
      getUserMedia.mockImplementation(
        async (constraints: MediaStreamConstraints) =>
          stream(
            constraints.video ? "video" : "audio",
            String(requestedDevice(constraints).exact),
          ),
      );
      fireEvent.click(screen.getByRole("button", { name: retry }));
      await waitFor(() =>
        expect(tracks.filter((track) => track.kind === kind)).toHaveLength(2),
      );
      expect(attempts()).toHaveLength(4);
      expect(
        screen.queryByRole("button", { name: retry }),
      ).not.toBeInTheDocument();
      expect(unaffected.stop).not.toHaveBeenCalled();
    },
  );

  it("enumerates five webcams but requests only the selected preview camera", async () => {
    const devices = await enumerateDevices();
    enumerateDevices.mockResolvedValue([
      ...devices,
      device("camera-third", "videoinput", "Third webcam"),
      device("camera-fourth", "videoinput", "Fourth webcam"),
      device("camera-fifth", "videoinput", "Fifth webcam"),
    ]);
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await screen.findByRole("button", { name: "Retry camera" });
    expect(
      getUserMedia.mock.calls.filter(([constraints]) => constraints.video),
    ).toEqual([
      [
        {
          video: {
            width: { ideal: 1280 },
            height: { ideal: 720 },
            frameRate: { ideal: 60 },
          },
          audio: false,
        },
      ],
    ]);
    fireEvent.click(
      screen.getByRole("button", { name: "Refresh device list" }),
    );
    await waitFor(() =>
      expect(
        screen.getByRole("option", { name: "Fifth webcam" }),
      ).toBeEnabled(),
    );
    expect(
      getUserMedia.mock.calls.filter(([constraints]) => constraints.video),
    ).toHaveLength(1);
    fireEvent.change(screen.getByLabelText("Camera", { exact: true }), {
      target: { value: "camera-fifth" },
    });
    await waitFor(() =>
      expect(
        screen.queryByText("Starting camera preview…"),
      ).not.toBeInTheDocument(),
    );
    expect(
      getUserMedia.mock.calls.filter(([constraints]) => constraints.video),
    ).toHaveLength(2);
    expect(getUserMedia).toHaveBeenLastCalledWith({
      video: {
        width: { ideal: 1280 },
        height: { ideal: 720 },
        frameRate: { ideal: 60 },
        deviceId: { exact: "camera-fifth" },
      },
      audio: false,
    });
  });

  it("uses a camera index from the query before the saved camera, and lets a UI change override it", async () => {
    localStorage.setItem(
      "ascii-chat.media-devices",
      JSON.stringify({ cameraId: "camera-busy" }),
    );
    window.history.replaceState(
      {},
      "",
      "/client?devices=true&videoDeviceIndex=1",
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await waitFor(() =>
      expect(
        screen.queryByText("Starting camera preview…"),
      ).not.toBeInTheDocument(),
    );
    expect(
      getUserMedia.mock.calls.filter(([constraints]) => constraints.video),
    ).toEqual([
      [
        {
          video: {
            width: { ideal: 1280 },
            height: { ideal: 720 },
            frameRate: { ideal: 60 },
            deviceId: { exact: "camera-free" },
          },
          audio: false,
        },
      ],
    ]);
    fireEvent.change(screen.getByLabelText("Camera", { exact: true }), {
      target: { value: "camera-busy" },
    });
    await screen.findByRole("button", { name: "Retry camera" });
    expect(new URLSearchParams(location.search).has("videoDeviceIndex")).toBe(
      false,
    );
  });

  it("requests a query-selected camera even when enumeration has not exposed its ID", async () => {
    window.history.replaceState(
      {},
      "",
      "/client?devices=true&cameraId=camera-hidden",
    );
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) => {
        if (constraints.video)
          throw new DOMException("No matching camera", "NotFoundError");
        return stream("audio", "mic-one");
      },
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await screen.findByRole("button", { name: "Retry camera" });
    expect(
      getUserMedia.mock.calls.filter(([constraints]) => constraints.video),
    ).toEqual([
      [
        {
          video: {
            width: { ideal: 1280 },
            height: { ideal: 720 },
            frameRate: { ideal: 60 },
            deviceId: { exact: "camera-hidden" },
          },
          audio: false,
        },
      ],
    ]);
    expect(screen.getByLabelText("Camera", { exact: true })).toHaveValue(
      "camera-hidden",
    );
  });

  it("explains camera startup timeouts without blocking microphone selection", async () => {
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) => {
        if (constraints.video)
          throw new DOMException("Timeout starting video source", "AbortError");
        return stream("audio", "mic-one");
      },
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    expect(
      await screen.findByRole("option", {
        name: "Busy webcam — Startup timed out (unavailable for this tab)",
      }),
    ).toBeEnabled();
    expect(screen.getByRole("status")).toHaveTextContent(
      "Startup timed out. This device may be in use by another tab or app.",
    );
    expect(screen.getByLabelText("Camera", { exact: true })).toBeEnabled();
    expect(screen.getByLabelText("Microphone", { exact: true })).toBeEnabled();
    expect(screen.getByRole("option", { name: "Other webcam" })).toBeEnabled();
    expect(screen.getByRole("button", { name: "Retry camera" })).toBeEnabled();
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
  });

  it("keeps System default selected instead of pinning the microphone alias", async () => {
    const devices = await enumerateDevices();
    enumerateDevices.mockResolvedValue([
      device("default", "audioinput", "Default - Headset microphone"),
      ...devices,
    ]);
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
    expect(screen.getByLabelText("Microphone", { exact: true })).toHaveValue(
      "",
    );
    expect(
      screen
        .getAllByRole("option")
        .filter((option) => option.textContent === "Headset microphone"),
    ).toHaveLength(1);
  });

  it("labels a busy default camera when permission reveals its identity after access fails", async () => {
    enumerateDevices.mockResolvedValueOnce([
      device("", "videoinput", ""),
      device("", "audioinput", ""),
    ]);
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) => {
        if (constraints.video)
          throw new DOMException("Device in use", "NotReadableError");
        return stream("audio", "mic-one");
      },
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    expect(
      await screen.findByRole("option", {
        name: "Busy webcam — In use (unavailable for this tab)",
      }),
    ).toBeEnabled();
    expect(screen.getByLabelText("Camera", { exact: true })).toHaveValue("");
    expect(screen.getByRole("option", { name: "Other webcam" })).toBeEnabled();
  });

  it("lists all devices when the default camera is busy and keeps microphone access working", async () => {
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    const busy = await screen.findByRole("option", {
      name: "Busy webcam — In use (unavailable for this tab)",
    });
    expect(busy).toBeEnabled();
    expect(screen.getByLabelText("Camera", { exact: true })).toBeEnabled();
    expect(screen.getByLabelText("Microphone", { exact: true })).toBeEnabled();
    expect(screen.getByRole("option", { name: "Other webcam" })).toBeEnabled();
    expect(
      screen.getByRole("option", { name: "Headset microphone" }),
    ).toBeEnabled();
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
    expect(screen.getByRole("button", { name: "Test speakers" })).toBeEnabled();
    expect(
      getUserMedia.mock.calls.every(
        ([constraints]) => !(constraints.video && constraints.audio),
      ),
    ).toBe(true);

    const microphone = tracks.find((track) => track.kind === "audio")!;
    fireEvent.change(screen.getByLabelText("Camera", { exact: true }), {
      target: { value: "camera-free" },
    });
    await waitFor(() =>
      expect(
        screen.queryByText("Camera preview unavailable"),
      ).not.toBeInTheDocument(),
    );
    expect(microphone.stop).not.toHaveBeenCalled();
    expect(
      screen.queryByRole("button", { name: "Retry camera" }),
    ).not.toBeInTheDocument();
    expect(busy).toBeEnabled();
  });

  it("switches microphones without restarting the working camera", async () => {
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) => {
        const id = String(requestedDevice(constraints).exact);
        if (id === "mic-one")
          throw new DOMException("Device in use", "NotReadableError");
        return stream(constraints.video ? "video" : "audio", id);
      },
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await screen.findByRole("button", { name: "Retry microphone" });
    expect(
      screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
    ).toBeDisabled();
    const camera = tracks.find((track) => track.kind === "video")!;
    fireEvent.change(screen.getByLabelText("Microphone", { exact: true }), {
      target: { value: "mic-two" },
    });
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
    expect(camera.stop).not.toHaveBeenCalled();
    fireEvent.click(
      screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
    );
    expect(resume).toHaveBeenCalled();
  });

  it("retries a busy camera without interrupting the microphone", async () => {
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    const retry = await screen.findByRole("button", { name: "Retry camera" });
    const microphone = tracks.find((track) => track.kind === "audio")!;
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) =>
        stream(
          constraints.video ? "video" : "audio",
          String(requestedDevice(constraints).exact),
        ),
    );
    fireEvent.click(retry);
    await waitFor(() =>
      expect(
        screen.queryByText("Camera preview unavailable"),
      ).not.toBeInTheDocument(),
    );
    expect(screen.getByRole("option", { name: "Busy webcam" })).toBeEnabled();
    expect(microphone.stop).not.toHaveBeenCalled();
  });

  it("shows permission errors per input while keeping device choices visible", async () => {
    getUserMedia.mockRejectedValue(
      new DOMException("Permission denied", "NotAllowedError"),
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await waitFor(() =>
      expect(screen.getAllByText(/Access denied/)).toHaveLength(2),
    );
    expect(screen.getByLabelText("Camera", { exact: true })).toBeEnabled();
    expect(screen.getByLabelText("Microphone", { exact: true })).toBeEnabled();
    expect(screen.queryByText(/In use/)).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Test speakers" })).toBeEnabled();
  });

  it("refreshes devices after hotplug and lets unavailable choices be tried again", async () => {
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await screen.findByRole("option", {
      name: "Busy webcam — In use (unavailable for this tab)",
    });
    const devices = await enumerateDevices();
    enumerateDevices.mockResolvedValue([
      ...devices,
      device("camera-new", "videoinput", "USB webcam"),
    ]);
    act(() => mediaDevices.dispatchEvent(new Event("devicechange")));
    expect(
      await screen.findByRole("option", { name: "USB webcam" }),
    ).toBeEnabled();
    fireEvent.click(
      screen.getByRole("button", { name: "Refresh device list" }),
    );
    expect(
      await screen.findByRole("option", { name: "Busy webcam" }),
    ).toBeEnabled();
  });

  it("stops streams returned after the dialog closes", async () => {
    let resolveCamera!: (value: Stream) => void;
    const pendingCamera = new Promise<Stream>((resolve) => {
      resolveCamera = resolve;
    });
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) =>
        constraints.video ? pendingCamera : stream("audio", "mic-one"),
    );
    const view = render(<DeviceSetupModal open onClose={vi.fn()} />);
    await waitFor(() => expect(getUserMedia).toHaveBeenCalledTimes(2));
    view.rerender(<DeviceSetupModal open={false} onClose={vi.fn()} />);
    await act(async () => resolveCamera(stream("video", "camera-busy")));
    expect(tracks.every((track) => track.stop.mock.calls.length === 1)).toBe(
      true,
    );
    expect(closeAudio).toHaveBeenCalled();
  });

  it("discards an older camera request after a different camera is selected", async () => {
    let resolveCamera!: (value: Stream) => void;
    const pendingCamera = new Promise<Stream>((resolve) => {
      resolveCamera = resolve;
    });
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) => {
        const id = String(requestedDevice(constraints).exact);
        return id === "camera-busy"
          ? pendingCamera
          : stream(constraints.video ? "video" : "audio", id);
      },
    );
    render(<DeviceSetupModal open onClose={vi.fn()} />);
    await waitFor(() => expect(getUserMedia).toHaveBeenCalledTimes(2));
    fireEvent.change(screen.getByLabelText("Camera", { exact: true }), {
      target: { value: "camera-free" },
    });
    await waitFor(() =>
      expect(
        screen.queryByText("Starting camera preview…"),
      ).not.toBeInTheDocument(),
    );
    const oldStream = stream("video", "camera-busy");
    await act(async () => resolveCamera(oldStream));
    expect(oldStream.getTracks()[0]!.stop).toHaveBeenCalled();
    expect(screen.getByLabelText("Camera", { exact: true })).toHaveValue(
      "camera-free",
    );
    expect(
      tracks.find((track) => track.id === "camera-free")!.stop,
    ).not.toHaveBeenCalled();
  });
});

it("offers System default for both inputs and preserves it after preview and save", async () => {
  getUserMedia.mockImplementation(async (constraints: MediaStreamConstraints) =>
    stream(
      constraints.video ? "video" : "audio",
      constraints.video ? "camera-free" : "mic-two",
    ),
  );
  render(<DeviceSetupModal open onClose={vi.fn()} />);
  await waitFor(() => expect(tracks).toHaveLength(2));
  await waitFor(() =>
    expect(
      screen.getByRole("button", { name: /^(?:Test microphone|Stop test)$/ }),
    ).toBeEnabled(),
  );
  expect(screen.getByLabelText("Camera", { exact: true })).toHaveValue("");
  expect(screen.getByLabelText("Microphone", { exact: true })).toHaveValue("");
  expect(
    screen.getAllByRole("option", { name: "System default" }),
  ).toHaveLength(3);
  const requests = getUserMedia.mock.calls.map(([constraints]) => constraints);
  expect(requests.find((constraints) => constraints.audio).audio).toBe(true);
  expect(
    requests.find((constraints) => constraints.video).video,
  ).not.toHaveProperty("deviceId");
  fireEvent.click(screen.getByRole("button", { name: "Done" }));
  expect(
    JSON.parse(localStorage.getItem("ascii-chat.media-devices")!),
  ).toMatchObject({ cameraId: "", microphoneId: "" });
});

describe("shared Device Setup without chat URL policy", () => {
  it("uses saved devices, ignores URL overrides, and saves without changing the URL", async () => {
    const { DeviceSetupModal: SharedDeviceSetupModal } =
      await import("@ascii-chat/shared/components/DeviceSetupModal");
    localStorage.setItem(
      "ascii-chat.media-devices",
      JSON.stringify({
        cameraId: "camera-free",
        microphoneId: "mic-two",
        speakerId: "default",
      }),
    );
    window.history.replaceState(
      {},
      "",
      "/docs/display?cameraId=camera-busy&videoDeviceIndex=0#face-follow",
    );
    const url = location.href;
    const onClose = vi.fn();
    render(<SharedDeviceSetupModal open onClose={onClose} />);
    await waitFor(() =>
      expect(screen.getByLabelText("Camera", { exact: true })).toHaveValue(
        "camera-free",
      ),
    );
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
    expect(screen.getByLabelText("Microphone", { exact: true })).toHaveValue(
      "mic-two",
    );
    expect(screen.queryByLabelText("Video encoding")).not.toBeInTheDocument();
    fireEvent.change(screen.getByLabelText("Microphone", { exact: true }), {
      target: { value: "mic-one" },
    });
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
    fireEvent.click(screen.getByRole("button", { name: "Done" }));
    expect(onClose).toHaveBeenCalledOnce();
    expect(
      JSON.parse(localStorage.getItem("ascii-chat.media-devices")!),
    ).toEqual({
      cameraId: "camera-free",
      microphoneId: "mic-one",
      speakerId: "default",
    });
    expect(location.href).toBe(url);
  });
  it("keeps defaults unpinned and releases previews when dismissed without saving", async () => {
    const { DeviceSetupModal: SharedDeviceSetupModal } =
      await import("@ascii-chat/shared/components/DeviceSetupModal");
    getUserMedia.mockImplementation(
      async (constraints: MediaStreamConstraints) =>
        stream(
          constraints.video ? "video" : "audio",
          constraints.video ? "camera-free" : "mic-two",
        ),
    );
    const { unmount } = render(
      <SharedDeviceSetupModal open onClose={vi.fn()} />,
    );
    await waitFor(() =>
      expect(
        screen.getByRole("button", { name: /^(Test microphone|Stop test)$/ }),
      ).toBeEnabled(),
    );
    expect(screen.getByLabelText("Camera", { exact: true })).toHaveValue("");
    expect(screen.getByLabelText("Microphone", { exact: true })).toHaveValue(
      "",
    );
    unmount();
    expect(tracks.every((track) => track.stop.mock.calls.length > 0)).toBe(
      true,
    );
    expect(localStorage.getItem("ascii-chat.media-devices")).toBeNull();
  });
});

it("restores a saved microphone and speaker while permission hides their IDs", async () => {
  const { DeviceSetupModal: SharedDeviceSetupModal } =
    await import("@ascii-chat/shared/components/DeviceSetupModal");
  localStorage.setItem(
    "ascii-chat.media-devices",
    JSON.stringify({
      cameraId: "camera-free",
      microphoneId: "hidden-mic",
      speakerId: "hidden-speaker",
    }),
  );
  enumerateDevices.mockResolvedValue([
    device("", "videoinput", ""),
    device("", "audioinput", ""),
    device("", "audiooutput", ""),
  ]);
  getUserMedia.mockImplementation(async (constraints: MediaStreamConstraints) =>
    stream(
      constraints.video ? "video" : "audio",
      constraints.video ? "camera-free" : "hidden-mic",
    ),
  );
  render(<SharedDeviceSetupModal open onClose={vi.fn()} />);
  await waitFor(() =>
    expect(
      getUserMedia.mock.calls.some(
        ([constraints]) => constraints.audio?.deviceId?.exact === "hidden-mic",
      ),
    ).toBe(true),
  );
  await waitFor(() =>
    expect(screen.getByLabelText("Microphone", { exact: true })).toHaveValue(
      "hidden-mic",
    ),
  );
  expect(screen.getByLabelText("Speakers", { exact: true })).toHaveValue(
    "hidden-speaker",
  );
  fireEvent.click(screen.getByRole("button", { name: "Done" }));
  expect(
    JSON.parse(localStorage.getItem("ascii-chat.media-devices")!),
  ).toMatchObject({ microphoneId: "hidden-mic", speakerId: "hidden-speaker" });
});
