import React from "react";

interface TooltipProps {
  text?: string | undefined;
  children: React.ReactNode;
}

export function Tooltip({ text, children }: TooltipProps) {
  if (!text) {
    return <>{children}</>;
  }

  return (
    <div className="group relative inline-block w-full">
      {children}
      <div className="pointer-events-none invisible absolute bottom-full z-10 mb-2 w-48 whitespace-normal rounded bg-gray-800 p-2 text-xs text-white group-hover:visible">
        {text}
      </div>
    </div>
  );
}
