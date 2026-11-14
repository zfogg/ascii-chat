import { useId } from "react";
import { Tooltip } from "./Tooltip";

export function HelpIcon({ label, text }: { label: string; text: string }) {
  const id = useId();
  return (
    <Tooltip
      text={text}
      id={id}
      inline
      showOnFocus
      className="inline-flex shrink-0"
    >
      <button
        type="button"
        aria-label={`${label} help`}
        aria-describedby={id}
        className="inline-flex h-4 w-4 items-center justify-center rounded-full border border-terminal-8 text-[10px] text-terminal-8 hover:text-terminal-fg focus-visible:outline focus-visible:outline-terminal-4"
      >
        <span aria-hidden="true">i</span>
      </button>
    </Tooltip>
  );
}
