import "@testing-library/jest-dom/vitest";
import {
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vite-plus/test";
import { SessionInvite } from "../../src/components/SessionInvite";

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe("Session invitation", () => {
  it("preserves custom TURN credentials with the password in the fragment", () => {
    window.history.replaceState(
      {},
      "",
      "/discovery?turnUrls=turn%3Arelay.example&turnUsername=alice&connectionRoute=relay#turnCredential=p%2Bass",
    );
    render(
      <SessionInvite
        sessionName="quiet-river-otter"
        signalingUrl="wss://discovery.example/ws"
        sessionPassword="session"
      />,
    );
    const url = new URL(
      (screen.getByLabelText("Join link") as HTMLInputElement).value,
    );
    expect(url.searchParams.get("turnUrls")).toBe("turn:relay.example");
    expect(url.searchParams.get("turnUsername")).toBe("alice");
    expect(url.searchParams.get("connectionRoute")).toBe("relay");
    expect(url.searchParams.has("turnCredential")).toBe(false);
    expect(new URLSearchParams(url.hash.slice(1)).get("turnCredential")).toBe(
      "p+ass",
    );
    expect(new URLSearchParams(url.hash.slice(1)).get("sessionPassword")).toBe(
      "session",
    );
  });
  it("copies the confirmed name and a join URL with the password only in its fragment", async () => {
    window.history.replaceState(
      {},
      "",
      "/discovery?connect=true&turnUrls=&hostServerUrl=ws://private#sessionPassword=secret",
    );
    const writeText = vi.fn().mockResolvedValue(undefined);
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: { writeText },
    });
    render(
      <SessionInvite
        sessionName="quiet-river-otter"
        signalingUrl="wss://discovery.example/ws"
        sessionPassword="p+a&ss# word/雪"
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: "Copy name" }));
    await waitFor(() =>
      expect(screen.getByRole("status")).toHaveTextContent(
        "Session name copied.",
      ),
    );
    expect(writeText).toHaveBeenLastCalledWith("quiet-river-otter");
    fireEvent.click(screen.getByRole("button", { name: "Copy join link" }));
    const url = new URL(writeText.mock.calls[1]![0] as string);
    expect(url.searchParams.get("session")).toBe("quiet-river-otter");
    expect(url.searchParams.get("signalingUrl")).toBe(
      "wss://discovery.example/ws",
    );
    expect(url.searchParams.has("connect")).toBe(false);
    expect(new URLSearchParams(url.hash.slice(1)).get("sessionPassword")).toBe(
      "p+a&ss# word/雪",
    );
    expect(url.searchParams.has("sessionPassword")).toBe(false);
    expect(url.searchParams.get("turnUrls")).toBe("");
    expect(url.searchParams.has("hostServerUrl")).toBe(false);
    expect(screen.getByLabelText("Join link")).toHaveValue(url.href);
  });

  it("leaves the link selectable when clipboard access fails", async () => {
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: { writeText: vi.fn().mockRejectedValue(new Error("Denied")) },
    });
    render(
      <SessionInvite
        sessionName="quiet-river-otter"
        signalingUrl="wss://discovery.example/ws"
        sessionPassword=""
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: "Copy join link" }));
    await waitFor(() =>
      expect(screen.getByRole("status")).toHaveTextContent("Select and copy"),
    );
    expect(screen.getByLabelText("Join link")).toHaveAttribute("readonly");
    const input = screen.getByLabelText("Join link") as HTMLInputElement;
    expect(new URL(input.value).hash).toBe("");
  });
});
