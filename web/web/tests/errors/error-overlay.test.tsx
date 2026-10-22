import { act, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import { ErrorOverlay } from "../../src/components/ErrorOverlay";
import {
  forwardJavaScriptError,
  publishBrowserError,
  reportJavaScriptError,
  reportProtocolError,
  setNetworkErrorReporter,
  sourceUrlForFrame,
} from "../../src/errors/errorBus";

afterEach(() => setNetworkErrorReporter(null));

describe("browser error overlay", () => {
  it("renders every C stack frame and copies its debug-only full path", () => {
    const dismiss = vi.fn();
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: { writeText },
    });
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
            fullFile: "C:/src/ascii-chat/src/web/client.c",
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
    const outerPath = screen.getByRole("button", {
      name: "Copy full path for src/web/client.c",
    });
    const outerMetadata = outerPath.parentElement!;
    const outerMessage = screen.getByText("outer failure");
    expect(
      outerMetadata.compareDocumentPosition(outerMessage) &
        Node.DOCUMENT_POSITION_FOLLOWING,
    ).toBeTruthy();
    expect(outerMetadata.closest("li")?.className).toContain("border");
    expect(outerPath.textContent).toBe("src/web/client.c:1196");
    expect(outerPath.className).toContain("underline");
    expect(outerPath.querySelector(".text-blue-400")?.textContent).toBe(
      "src/web/client.c",
    );
    expect(outerPath.querySelector(".text-terminal-fg")?.textContent).toBe(":");
    expect(outerPath.querySelector(".text-purple-400")?.textContent).toBe(
      "1196",
    );
    fireEvent.click(outerPath);
    expect(writeText).toHaveBeenCalledWith(
      "C:/src/ascii-chat/src/web/client.c",
    );
    expect(screen.getByText("client_validate_frame()").className).toContain(
      "text-orange-400",
    );
    expect(screen.getAllByText("in")[0]?.className).toContain(
      "text-terminal-fg",
    );
    expect(screen.getByText("lib/network/packet/packet.c")).toBeTruthy();
    expect(screen.getByText("481").className).toContain("text-purple-400");
    fireEvent.click(screen.getByRole("button", { name: "Dismiss" }));
    expect(dismiss).toHaveBeenCalledTimes(1);
    expect(screen.queryByRole("alertdialog")).toBeNull();
  });

  it("shows remote protocol errors one line per frame", () => {
    render(<ErrorOverlay />);
    act(() => {
      reportProtocolError(
        49,
        "server wrapper (src/server/client.c:144 in run())\tfull-path=C:/src/ascii-chat/src/server/client.c\nsocket cause (lib/network/tcp.c:91 in receive())",
      );
    });
    expect(screen.getByText("server wrapper")).toBeTruthy();
    const serverPath = screen.getByRole("button", {
      name: "Copy full path for src/server/client.c",
    });
    expect(serverPath.textContent).toBe("src/server/client.c:144");
    expect(screen.getByText("run()")).toBeTruthy();
    expect(screen.getByText("socket cause")).toBeTruthy();
    expect(screen.getByText("lib/network/tcp.c")).toBeTruthy();
    expect(screen.getByText("91").className).toContain("text-purple-400");
    expect(screen.getByText("receive()")).toBeTruthy();
    expect(
      screen.getByRole("button", {
        name: "Copy full path for src/server/client.c",
      }),
    ).toBeTruthy();
  });

  it("links production source locations to the deployed GitHub revision", () => {
    const revision = __COMMIT_SHA__ === "unknown" ? "master" : __COMMIT_SHA__;
    render(<ErrorOverlay productionMode />);
    act(() => {
      publishBrowserError({
        source: "C/WASM",
        title: "Production failure",
        frames: [
          {
            message: "failed",
            file: "lib/network/packet/packet.c",
            fullFile: "C:/private/ascii-chat/lib/network/packet/packet.c",
            function: "packet_validate",
            line: 188,
          },
        ],
      });
    });
    const source = screen.getByRole("link", {
      name: `Open lib/network/packet/packet.c line 188 on GitHub at ${revision}`,
    });
    expect(source.textContent).toBe("lib/network/packet/packet.c:188");
    expect(source.className).toContain("underline");
    expect(source.getAttribute("target")).toBe("_blank");
    expect(source.getAttribute("title")).toBe(
      `Open lib/network/packet/packet.c:188 on GitHub at ${revision}`,
    );
    expect(source.getAttribute("href")).toBe(
      sourceUrlForFrame({
        message: "failed",
        file: "lib/network/packet/packet.c",
        line: 188,
      }),
    );
    expect(screen.queryByRole("button", { name: /Copy full path/ })).toBeNull();
    expect(screen.queryByText(/C:\/private/)).toBeNull();
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
