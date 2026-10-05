import { useId } from "react";

export function HelpLabel({ label, text }: { label: string; text: string }) {
  const tooltipId = useId();
  return (
    <span
      tabIndex={0}
      aria-describedby={tooltipId}
      className="group relative inline-flex items-center gap-1 cursor-help focus-visible:outline focus-visible:outline-terminal-4"
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
      <span
        id={tooltipId}
        role="tooltip"
        className="invisible absolute left-0 top-full z-50 mt-1 w-80 max-w-[calc(100vw-2rem)] whitespace-normal break-words rounded bg-gray-800 p-2 text-xs font-normal text-white shadow-lg opacity-0 group-hover:visible group-hover:opacity-100 group-focus:visible group-focus:opacity-100"
      >
        {text}
      </span>
    </span>
  );
}
