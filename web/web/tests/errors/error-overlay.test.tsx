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
    expect(screen.getByText("Stack order: 0 is newest")).toBeTruthy();
    expect(screen.getByTitle("Newest error").textContent).toBe("0");
    expect(screen.getByTitle("Stack frame 1").textContent).toBe("1");
    expect(screen.getByText("checksum mismatch")).toBeTruthy();
    const outerPath = screen.getByText("src/web/client.c");
    const outerMetadata = outerPath.parentElement!;
    const outerMessage = screen.getByText("outer failure");
    expect(
      outerMetadata.compareDocumentPosition(outerMessage) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).toBeTruthy();
    expect(outerMetadata.closest("li")?.className).toContain("border");
    expect(outerPath.className).toContain("text-blue-400");
    expect(screen.getByText("1196").className).toContain("text-purple-400");
    expect(screen.getByText("client_validate_frame()").className).toContain(
      "text-orange-400",
    );
    expect(screen.getAllByText("in")[0]?.className).toContain(
      "text-terminal-fg",
    );
    expect(screen.getByText("lib/network/packet/packet.c")).toBeTruthy();
    expect(screen.getByText("481")).toBeTruthy();
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
    expect(screen.getByText("server wrapper")).toBeTruthy();
    expect(screen.getByText("src/server/client.c")).toBeTruthy();
    expect(screen.getByText("144")).toBeTruthy();
    expect(screen.getByText("run()")).toBeTruthy();
    expect(screen.getByText("socket cause")).toBeTruthy();
    expect(screen.getByText("lib/network/tcp.c")).toBeTruthy();
    expect(screen.getByText("91")).toBeTruthy();
    expect(screen.getByText("receive()")).toBeTruthy();
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
