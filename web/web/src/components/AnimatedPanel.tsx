import { type ReactNode } from "react";

/** Animate layout height while keeping form values and focus order intact. */
export function AnimatedPanel({
  open,
  children,
  id,
}: {
  open: boolean;
  children: ReactNode;
  id?: string;
}) {
  return (
    <div id={id} className="animated-panel" data-open={open} inert={!open}>
      <div className="animated-panel-content">{children}</div>
    </div>
  );
}

export function PanelChevron({ open }: { open: boolean }) {
  return (
    <svg
      aria-hidden="true"
      viewBox="0 0 16 16"
      width="16"
      height="16"
      className="panel-chevron"
      data-open={open}
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
    >
      <path d="m6 3 5 5-5 5" />
    </svg>
  );
}
