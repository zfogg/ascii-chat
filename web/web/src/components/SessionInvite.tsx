import { useState } from "react";

export function SessionInvite({
  sessionName,
  signalingUrl,
  sessionPassword,
}: {
  sessionName: string;
  signalingUrl: string;
  sessionPassword: string;
}) {
  const [feedback, setFeedback] = useState("");
  const invite = new URL("/discovery", window.location.origin);
  invite.searchParams.set("session", sessionName);
  invite.searchParams.set("signalingUrl", signalingUrl);
  if (sessionPassword) {
    invite.hash = new URLSearchParams({ sessionPassword }).toString();
  }
  const current = new URL(window.location.href);
  for (const key of ["stunUrls", "turnUrls", "connectionRoute"]) {
    const value = current.searchParams.get(key);
    if (value !== null) invite.searchParams.set(key, value);
  }

  if (current.searchParams.has("turnUrls")) {
    const fragment = new URLSearchParams(invite.hash.slice(1));
    const currentFragment = new URLSearchParams(current.hash.slice(1));
    for (const key of ["turnUsername", "turnCredential"]) {
      const value = currentFragment.get(key) ?? current.searchParams.get(key);
      if (value) {
        if (key === "turnUsername") invite.searchParams.set(key, value);
        else fragment.set(key, value);
      }
    }
    invite.hash = fragment.toString();
  }

  const copy = async (value: string, label: string) => {
    try {
      await navigator.clipboard.writeText(value);
      setFeedback(`${label} copied.`);
    } catch {
      setFeedback("Clipboard unavailable. Select and copy the text below.");
    }
  };

  return (
    <section
      aria-label="Invite a participant"
      className="border-l-2 border-blue-400 pl-4 py-1 my-2 min-w-0"
    >
      <div className="flex flex-wrap items-baseline gap-x-3">
        <h2 className="font-semibold">Invite a friend</h2>
        <p className="text-sm text-terminal-7">
          Keep this tab open for the call.
        </p>
      </div>
      <div className="flex flex-wrap items-center gap-3 mt-2">
        <span className="font-mono text-xl select-all break-all">
          {sessionName}
        </span>
        <button
          type="button"
          onClick={() => void copy(sessionName, "Session name")}
          className="border border-terminal-8 rounded px-3 py-2 cursor-pointer hover:bg-terminal-8/20"
        >
          Copy name
        </button>
        <button
          type="button"
          onClick={() => void copy(invite.href, "Join link")}
          className="bg-blue-600 text-white rounded px-3 py-2 cursor-pointer hover:bg-blue-700"
        >
          Copy join link
        </button>
      </div>
      <label className="flex items-center gap-2 mt-2 text-sm max-w-2xl">
        <span className="shrink-0">Join link</span>
        <input
          readOnly
          value={invite.href}
          onFocus={(event) => event.currentTarget.select()}
          className="font-mono bg-terminal-bg border border-terminal-8 rounded px-2 py-1 min-w-0 w-full"
        />
      </label>
      <p
        role="status"
        aria-live="polite"
        className="text-sm text-terminal-7 mt-1 min-h-5"
      >
        {feedback ||
          (sessionPassword
            ? "The join link includes the session password."
            : "Share the session name or join link.")}
      </p>
    </section>
  );
}
