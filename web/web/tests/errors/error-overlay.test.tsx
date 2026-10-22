import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import { ErrorOverlay } from "../../src/components/ErrorOverlay";
import {
  forwardJavaScriptError,
  publishBrowserError,
  reportJavaScriptError,
  reportProtocolError,
  setNetworkErrorReporter,
} from "../../src/errors/errorBus";

afterEach(() => setNetworkErrorReporter(null));

describe("browser error overlay", () => {
  it("renders every C stack frame with source location and dismisses it", () => {
    const dismiss = vi.fn();
    render(<ErrorOverlay />);
    act(() => {
      publishBrowserError({
        source: "C/WASM",
        title: "Frame validation failed (17)",
        dismiss,
        frames: [
          {
            code: 17,
            message: "outer failure",
            file: "src/web/client.c",
            function: "client_validate_frame",
            line: 1196,
          },
          {
            code: 49,
            message: "checksum mismatch",
            file: "lib/network/packet/packet.c",
            function: "packet_validate",
            line: 481,
          },
        ],
      });
    });

    expect(screen.getByText("outer failure")).toBeTruthy();
    expect(screen.getByText("checksum mismatch")).toBeTruthy();
    expect(screen.getByText(/src\/web\/client.c:1196/)).toBeTruthy();
    expect(screen.getByText(/packet.c:481/)).toBeTruthy();
    fireEvent.click(screen.getByRole("button", { name: "Dismiss" }));
    expect(dismiss).toHaveBeenCalledTimes(1);
    expect(screen.queryByRole("alertdialog")).toBeNull();
  });

  it("shows remote protocol errors one line per frame", () => {
    render(<ErrorOverlay />);
    act(() => {
      reportProtocolError(
        49,
        "server wrapper (src/server/client.c:144 in run())\nsocket cause (lib/network/tcp.c:91 in receive())",
      );
    });
    expect(screen.getByText(/server wrapper/)).toBeTruthy();
    expect(screen.getByText(/socket cause/)).toBeTruthy();
  });

  it("forwards JavaScript errors through the active network reporter", () => {
    const reporter = vi.fn();
    setNetworkErrorReporter(reporter);
    reportJavaScriptError(new Error("encoder exploded"));
    expect(reporter).toHaveBeenCalledTimes(1);
    expect(reporter.mock.calls[0]?.[0].frames[0]?.message).toBe(
      "encoder exploded",
    );
  });

  it("can forward an already-rendered C error without queuing a duplicate", () => {
    const reporter = vi.fn();
    setNetworkErrorReporter(reporter);
    render(<ErrorOverlay />);
    forwardJavaScriptError(new Error("C boundary threw"));
    expect(reporter).toHaveBeenCalledTimes(1);
    expect(screen.queryByRole("alertdialog")).toBeNull();
  });
});
