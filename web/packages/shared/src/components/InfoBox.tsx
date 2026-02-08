import type { HTMLAttributes } from "react";

interface InfoBoxProps extends Omit<HTMLAttributes<HTMLDivElement>, 'className'> {
  variant?: "note" | "warning" | "error" | "success" | "info";
  className?: string;
}

export function InfoBox({ variant = "note", children, className = "", ...props }: InfoBoxProps) {
  const boxClass = `info-box-${variant}`;
  const textClass = `info-text-${variant}`;
  const combinedClassName = `${boxClass} ${className}`.trim();

  return (
    <div className={combinedClassName} {...props}>
      <div className={textClass}>{children}</div>
    </div>
  );
}
