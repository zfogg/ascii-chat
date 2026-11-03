import "@testing-library/jest-dom/vitest";
import {
  act,
  cleanup,
  fireEvent,
  render,
  screen,
} from "@testing-library/react";
import { afterEach, describe, expect, it } from "vite-plus/test";
import { ModeInstructions } from "../../src/components/ModeInstructions";
import { Header } from "../../src/components/Header";

afterEach(() => {
  cleanup();
  window.history.replaceState(null, "", "/");
});

describe("mode instructions", () => {
  it.each(["mirror", "client", "discovery"])(
    "shows help in the %s header",
    (mode) => {
      window.history.replaceState(null, "", `/${mode}`);
      render(<Header />);
      expect(
        screen.getByRole("button", { name: `Open ${mode} instructions` }),
      ).toBeVisible();
    },
  );

  it("does not show mode instructions on the home page", () => {
    window.history.replaceState(null, "", "/");
    render(<Header />);
    expect(
      screen.queryByRole("button", { name: /instructions/i }),
    ).not.toBeInTheDocument();
  });

  it("opens and closes through query state without losing other settings", () => {
    window.history.replaceState(null, "", "/mirror?width=80");
    render(<ModeInstructions mode="mirror" />);
    fireEvent.click(
      screen.getByRole("button", { name: "Open mirror instructions" }),
    );
    expect(screen.getByRole("dialog", { name: "Using Mirror" })).toBeVisible();
    expect(new URLSearchParams(location.search).get("help")).toBe("true");
    fireEvent.click(screen.getByRole("button", { name: "Close" }));
    expect(new URLSearchParams(location.search).get("help")).toBeNull();
    expect(new URLSearchParams(location.search).get("width")).toBe("80");
  });

  it("restores query changes and closes with Escape", () => {
    window.history.replaceState(null, "", "/client");
    render(<ModeInstructions mode="client" />);
    act(() => {
      window.history.replaceState(null, "", "/client?help=true");
      window.dispatchEvent(new PopStateEvent("popstate"));
    });
    expect(screen.getByRole("dialog", { name: "Using Client" })).toBeVisible();
    fireEvent.keyDown(document, { key: "Escape" });
    expect(new URLSearchParams(location.search).get("help")).toBeNull();
  });

  it("restores a shared link", () => {
    window.history.replaceState(null, "", "/discovery?help=true");
    render(<ModeInstructions mode="discovery" />);
    expect(
      screen.getByRole("dialog", { name: "Using Discovery" }),
    ).toBeVisible();
  });
});
