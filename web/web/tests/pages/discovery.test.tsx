import "@testing-library/jest-dom/vitest";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
  within,
} from "@testing-library/react";
import { HeadingProvider } from "@ascii-chat/shared/components";
import userEvent from "@testing-library/user-event";
import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { ClientPage } from "../../src/pages/Client";
import { ConnectionState } from "../../src/wasm/client";
import { LOCKED_SETTINGS_HELP } from "../../src/components/lockedSettings";
import type { DiscoveryOptions } from "../../src/network/WebRTCSession";

// Keep the page and its controls real; replace network, media, and WASM boundaries.
const backend = vi.hoisted(() => ({
  state: 0,
  status: "Disconnected",
  error: "",
  options: undefined as { discovery?: DiscoveryOptions } | undefined,
  connect: vi.fn<() => Promise<void>>(),
  disconnect: vi.fn(),
  stopWebcam: vi.fn(),
  startWebcam: vi.fn(),
  clientRef: { current: null },
  setError: vi.fn(),
}));
vi.mock("../../src/hooks", () => ({
  useClientConnection: (options: { discovery?: DiscoveryOptions }) => {
    backend.options = options;
    return {
      clientRef: backend.clientRef,
      connectionState: backend.state,
      status: backend.status,
      error: backend.error,
      publicKey: "",
      showModal: false,
      setShowModal: vi.fn(),
      setError: backend.setError,
      wasmInitialized: false,
      connectToServer: backend.connect,
      handleDisconnect: backend.disconnect,
    };
  },
  createWasmOptionsManager: vi.fn(),
  useCanvasCapture: () => ({ captureFrame: vi.fn() }),
  useRenderLoop: () => ({ startRenderLoop: vi.fn() }),
  useWebcamStream: () => ({
    startWebcam: backend.startWebcam,
    stopWebcam: backend.stopWebcam,
    isWebcamRunning: false,
  }),
}));
vi.mock("@ascii-chat/shared/wasm", async (importOriginal) => ({
  ...(await importOriginal<object>()),
  initMirrorWasm: vi.fn().mockResolvedValue(undefined),
}));
vi.mock("../../src/wasm/dist/mirror.js", () => ({ default: vi.fn() }));
vi.mock("../../src/components", async () => ({
  ...(await import("../../src/components/Settings")),
  ...(await import("../../src/components/PageLayout")),
  ...(await import("../../src/components/PageControlBar")),
  AsciiChatWebHead: () => null,
  AsciiRenderer: () => null,
  ConnectionPanelModal: () => null,
}));

const fieldNames = [
  "Discovery service URL",
  "Session name",
  "Session password",
  "Connection route",
  "STUN/TURN URLs",
  "TURN username",
  "TURN password",
];
async function openPage() {
  const view = render(<ClientPage discoveryMode />, {
    wrapper: HeadingProvider,
  });
  await act(async () => {});
  return view;
}
function expandConnectionSettings() {
  fireEvent.click(screen.getByText("Connection settings", { exact: true }));
}
function expectFieldsDisabled(disabled: boolean) {
  for (const name of fieldNames) {
    const input = screen.getByLabelText(name, { exact: true });
    if (disabled) expect(input).toBeDisabled();
    else expect(input).toBeEnabled();
  }
}
beforeEach(() => {
  vi.clearAllMocks();
  backend.state = ConnectionState.DISCONNECTED;
  backend.status = "Disconnected";
  backend.error = "";
  backend.connect.mockResolvedValue(undefined);
  window.history.replaceState({}, "", "/discovery");
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe("Discovery page", () => {
  it("loads the three primary fields in order with advanced settings collapsed", async () => {
    window.history.replaceState(
      {},
      "",
      "/discovery?session=blue-mountain-tiger&signalingUrl=ws%3A%2F%2Flocalhost%3A28227",
    );
    await openPage();
    const form = screen
      .getByRole("button", { name: "Join session" })
      .closest("form")!;
    expect(
      [...form.querySelectorAll(":scope > label input")].map((input) =>
        input.getAttribute("aria-label"),
      ),
    ).toEqual(fieldNames.slice(0, 3));
    expect(screen.getByLabelText("Session name", { exact: true })).toHaveValue(
      "blue-mountain-tiger",
    );
    expect(
      screen.getByLabelText("Discovery service URL", { exact: true }),
    ).toHaveValue("ws://localhost:28227");
    expect(form.querySelector("details")).not.toHaveAttribute("open");
    expectFieldsDisabled(false);
  });

  it("passes edited discovery settings to the connection hook", async () => {
    await openPage();
    expandConnectionSettings();
    for (const [name, value] of Object.entries({
      "Discovery service URL": "ws://localhost:28227",
      "Session name": "  blue-mountain-tiger  ",
      "Session password": "session-secret",
      "Connection route": "relay",
      "STUN/TURN URLs":
        " stun:example.com:3478, , turn:relay.example.com:3478 ",
      "TURN username": "alice",
      "TURN password": "turn-secret",
    }))
      fireEvent.change(screen.getByLabelText(name, { exact: true }), {
        target: { value },
      });
    await userEvent.click(screen.getByRole("button", { name: "Join session" }));
    expect(backend.connect).toHaveBeenCalledOnce();
    expect(backend.options?.discovery).toEqual({
      sessionName: "blue-mountain-tiger",
      password: "session-secret",
      signalingUrl: "ws://localhost:28227",
      iceTransportPolicy: "relay",
      turnUsername: "alice",
      turnCredential: "turn-secret",
      iceServers: [
        { urls: "stun:example.com:3478" },
        { urls: "turn:relay.example.com:3478" },
      ],
    });
  });

  it.each(["TURN username", "TURN password"])(
    "blocks submission when only %s is supplied",
    async (name) => {
      await openPage();
      expandConnectionSettings();
      fireEvent.change(screen.getByLabelText("Session name", { exact: true }), {
        target: { value: "blue-mountain-tiger" },
      });
      fireEvent.change(screen.getByLabelText(name, { exact: true }), {
        target: { value: "one-half" },
      });
      const button = screen.getByRole("button", { name: "Join session" });
      expect(button.closest("form")!.checkValidity()).toBe(false);
      await userEvent.click(button);
      expect(backend.connect).not.toHaveBeenCalled();
    },
  );

  it.each([
    ConnectionState.CONNECTING,
    ConnectionState.HANDSHAKE,
    ConnectionState.CONNECTED,
  ])(
    "locks discovery and rendering settings in connection state %s",
    async (state) => {
      backend.state = state;
      await openPage();
      expandConnectionSettings();
      expectFieldsDisabled(true);
      expect(
        screen.getByRole("button", { name: "Join session" }),
      ).toBeDisabled();
      await userEvent.click(screen.getByRole("button", { name: "Settings" }));
      const slider = screen.getByRole("slider");
      const panel = slider.closest(".settings-locked")!;
      expect(
        panel.querySelectorAll("input, select, button").length,
      ).toBeGreaterThanOrEqual(8);
      for (const control of panel.querySelectorAll("input, select, button"))
        expect(control).toBeDisabled();
    },
  );

  it("locks during a pending join and restores editing after failure", async () => {
    let reject!: (reason: Error) => void;
    backend.connect.mockImplementation(
      () =>
        new Promise<void>((_, rejectPromise) => {
          reject = rejectPromise;
        }),
    );
    await openPage();
    fireEvent.change(screen.getByLabelText("Session name", { exact: true }), {
      target: { value: "blue-mountain-tiger" },
    });
    await userEvent.click(screen.getByRole("button", { name: "Join session" }));
    expectFieldsDisabled(true);
    expect(screen.getByRole("button", { name: "Cancel" })).toBeEnabled();
    await act(async () => reject(new Error("Discovery unavailable")));
    expectFieldsDisabled(false);
    expect(
      screen.queryByRole("button", { name: "Cancel" }),
    ).not.toBeInTheDocument();
  });

  it("shows lock help only while disabled and unlocks after disconnect", async () => {
    backend.state = ConnectionState.CONNECTED;
    const view = await openPage();
    vi.useFakeTimers();
    const field = screen.getByLabelText("Discovery service URL", {
      exact: true,
    });
    fireEvent.mouseEnter(field.closest("label")!);
    act(() => vi.advanceTimersByTime(400));
    expect(screen.getByText(LOCKED_SETTINGS_HELP)).toHaveTextContent(
      LOCKED_SETTINGS_HELP,
    );
    fireEvent.click(screen.getByRole("button", { name: "Disconnect" }));
    expect(backend.disconnect).toHaveBeenCalledOnce();
    expect(backend.stopWebcam).toHaveBeenCalled();
    backend.state = ConnectionState.DISCONNECTED;
    view.rerender(<ClientPage discoveryMode />);
    expectFieldsDisabled(false);
    fireEvent.mouseEnter(
      screen
        .getByLabelText("Discovery service URL", { exact: true })
        .closest("label")!,
    );
    act(() => vi.advanceTimersByTime(400));
    expect(screen.queryByText(LOCKED_SETTINGS_HELP)).not.toBeInTheDocument();
  });

  it("keeps connection status in one place and displays backend errors", async () => {
    backend.status = "Connected over WebRTC (DTLS encrypted)";
    backend.error = "Session not found";
    backend.state = ConnectionState.ERROR;
    await openPage();
    expect(screen.getAllByText(backend.status)).toHaveLength(1);
    expect(screen.getByRole("alert")).toHaveTextContent("Session not found");
    expectFieldsDisabled(false);
  });

  it("preserves the quoted STUN/TURN example and credential help", async () => {
    await openPage();
    expandConnectionSettings();
    const help = screen.getAllByRole("tooltip", { hidden: true });
    expect(
      help.some((node) =>
        node.textContent?.includes(
          'Example: "stun:stun.example.com, stun:stun.example.com:3478, turn:turn.example.com:3478".',
        ),
      ),
    ).toBe(true);
    expect(
      help.some((node) =>
        node.textContent?.includes(
          "Leave both blank to use credentials from the discovery service",
        ),
      ),
    ).toBe(true);
    expect(screen.queryByText(LOCKED_SETTINGS_HELP)).not.toBeInTheDocument();
    expect(
      within(
        screen.getByRole("group", { name: /^TURN credentials/ }),
      ).getByLabelText("TURN password", {
        exact: true,
      }),
    ).toHaveAttribute("type", "password");
  });
});
