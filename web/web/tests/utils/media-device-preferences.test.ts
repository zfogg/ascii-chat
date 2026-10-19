import { beforeEach, expect, test, vi } from "vite-plus/test";
import { resolveCameraDeviceId } from "../../src/utils/mediaDevicePreferences";

const cameras = Array.from({ length: 5 }, (_, index) => ({
  kind: "videoinput",
  deviceId: `camera-${index}`,
  label: `Camera ${index + 1}`,
})) as MediaDeviceInfo[];
const enumerateDevices = vi.fn();
const getUserMedia = vi.fn();

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
  window.history.replaceState({}, "", "/client");
  enumerateDevices.mockResolvedValue(cameras);
  Object.defineProperty(navigator, "mediaDevices", {
    configurable: true,
    value: { enumerateDevices, getUserMedia },
  });
});

test.each([
  {
    selectedCameraId: "",
    search: "?cameraId=camera-3&videoDeviceIndex=2",
    stored: "camera-1",
    expected: "",
  },
  {
    selectedCameraId: "camera-4",
    search: "?cameraId=camera-3&videoDeviceIndex=2",
    stored: "camera-1",
    expected: "camera-4",
  },
  {
    selectedCameraId: undefined,
    search: "?cameraId=camera-3&videoDeviceIndex=2",
    stored: "camera-1",
    expected: "camera-3",
  },
  {
    selectedCameraId: undefined,
    search: "?videoDeviceIndex=2",
    stored: "camera-1",
    expected: "camera-2",
  },
  {
    selectedCameraId: undefined,
    search: "",
    stored: "camera-1",
    expected: "camera-1",
  },
  { selectedCameraId: undefined, search: "", stored: "", expected: "" },
])(
  "resolves camera priority: $search / $stored / $selectedCameraId",
  async ({ selectedCameraId, search, stored, expected }) => {
    window.history.replaceState({}, "", `/client${search}`);
    localStorage.setItem(
      "ascii-chat.media-devices",
      JSON.stringify({ cameraId: stored }),
    );
    expect(await resolveCameraDeviceId({ selectedCameraId })).toBe(expected);
    expect(getUserMedia).not.toHaveBeenCalled();
  },
);

test("keeps an explicit camera even when enumeration hides its ID before permission", async () => {
  enumerateDevices.mockResolvedValue([
    { kind: "videoinput", deviceId: "", label: "" },
  ]);
  window.history.replaceState({}, "", "/client?cameraId=not-yet-exposed");
  expect(await resolveCameraDeviceId()).toBe("not-yet-exposed");
  expect(enumerateDevices).not.toHaveBeenCalled();
  expect(getUserMedia).not.toHaveBeenCalled();
});

test("uses an anonymous browser default when permission hides all camera IDs", async () => {
  enumerateDevices.mockResolvedValue([
    { kind: "videoinput", deviceId: "", label: "" },
  ]);
  expect(await resolveCameraDeviceId()).toBe("");
  expect(getUserMedia).not.toHaveBeenCalled();
});

test.each(["-1", "abc", "1.5", "5"])(
  "rejects unavailable or invalid camera index %s without probing cameras",
  async (index) => {
    window.history.replaceState({}, "", `/client?videoDeviceIndex=${index}`);
    await expect(resolveCameraDeviceId()).rejects.toThrow();
    expect(getUserMedia).not.toHaveBeenCalled();
  },
);

test("uses a query snapshot if URL normalization removes a value matching local storage", async () => {
  localStorage.setItem(
    "ascii-chat.media-devices",
    JSON.stringify({ cameraId: "camera-3" }),
  );
  window.history.replaceState({}, "", "/client?videoDeviceIndex=2");
  expect(
    await resolveCameraDeviceId({
      search: "?cameraId=camera-3&videoDeviceIndex=2",
    }),
  ).toBe("camera-3");
});

test("reads camera IDs from older shared URLs without requesting device access", async () => {
  const search = `?mediaDevices=${encodeURIComponent(JSON.stringify({ cameraId: "camera-4" }))}`;
  expect(await resolveCameraDeviceId({ search })).toBe("camera-4");
  expect(getUserMedia).not.toHaveBeenCalled();
});
