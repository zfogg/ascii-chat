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
      className="border-l-2 border-blue-400 pl-4 py-2 my-3 min-w-0"
    >
      <h2 className="text-lg font-semibold">Invite a friend</h2>
      <p className="text-sm text-terminal-7 mt-1 mb-3">
        Share this session name or join link. Keep this tab open for the call.
      </p>
      <div className="flex flex-wrap items-center gap-3">
        <span className="font-mono text-xl sm:text-2xl select-all break-all">
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
      <label className="flex flex-col gap-1 mt-3 text-sm max-w-2xl">
        Join link
        <input
          readOnly
          value={invite.href}
          onFocus={(event) => event.currentTarget.select()}
          className="font-mono bg-terminal-bg border border-terminal-8 rounded px-3 py-2 min-w-0 w-full"
        />
      </label>
      {sessionPassword && (
        <p className="text-sm text-terminal-7 mt-2">
          The join link includes the session password.
        </p>
      )}
      <p role="status" aria-live="polite" className="text-sm mt-2 min-h-5">
        {feedback}
      </p>
    </section>
  );
}
