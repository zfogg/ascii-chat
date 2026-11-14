import { useId } from "react";
import { Tooltip } from "./Tooltip";

export function HelpLabel({ label, text }: { label: string; text: string }) {
  const tooltipId = useId();
  return (
    <Tooltip
      text={text}
      id={tooltipId}
      inline
      showOnFocus
      className="inline-flex"
    >
      <span
        tabIndex={0}
        aria-describedby={tooltipId}
        className="inline-flex items-center gap-1 cursor-help focus-visible:outline focus-visible:outline-terminal-4"
      >
        {label}
        <svg
          aria-hidden="true"
          viewBox="0 0 20 20"
          fill="none"
          stroke="currentColor"
          className="h-4 w-4 shrink-0 text-terminal-8"
        >
          <circle cx="10" cy="10" r="8" />
          <path d="M10 9v5" strokeWidth="1.5" />
          <circle cx="10" cy="6" r="0.75" fill="currentColor" stroke="none" />
        </svg>
      </span>
    </Tooltip>
  );
}
