import React, { useState, useRef, useCallback } from "react";
import { createPortal } from "react-dom";

interface TooltipProps {
  text?: string | undefined;
  children: React.ReactNode;
}

export function Tooltip({ text, children }: TooltipProps) {
  const [isVisible, setIsVisible] = useState(false);
  const [tooltipStyle, setTooltipStyle] = useState<React.CSSProperties>({});
  const timeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  const handleMouseEnter = useCallback(() => {
    timeoutRef.current = setTimeout(() => {
      setIsVisible(true);
    }, 350);
  }, []);

  const handleMouseMove = useCallback((e: React.MouseEvent) => {
    const tooltipWidth = 192; // w-48 = 12rem = 192px
    const tooltipHeight = 80; // approximate
    const offset = 12; // distance from cursor
    const viewportHeight = window.innerHeight;
    const viewportWidth = window.innerWidth;

    let top = e.clientY + offset;
    let left = e.clientX + offset;

    // Keep within viewport bounds
    if (top + tooltipHeight > viewportHeight) {
      top = e.clientY - tooltipHeight - offset;
    }
    if (left + tooltipWidth > viewportWidth) {
      left = e.clientX - tooltipWidth - offset;
    }

    // Ensure not off top/left
    top = Math.max(8, top);
    left = Math.max(8, left);

    setTooltipStyle({
      position: "fixed",
      top: `${top}px`,
      left: `${left}px`,
      width: `${tooltipWidth}px`,
      pointerEvents: "none",
    });
  }, []);

  const handleMouseLeave = useCallback(() => {
    if (timeoutRef.current) {
      clearTimeout(timeoutRef.current);
      timeoutRef.current = null;
    }
    setIsVisible(false);
  }, []);

  if (!text) {
    return <>{children}</>;
  }

  const tooltip = isVisible && (
    <div
      className="pointer-events-none z-50 whitespace-normal rounded bg-gray-800 p-2 text-xs text-white shadow-lg transition-opacity duration-200 visible opacity-100"
      style={tooltipStyle}
    >
      {text}
    </div>
  );

  return (
    <>
      <div
        ref={containerRef}
        className="flex-1"
        onMouseEnter={handleMouseEnter}
        onMouseLeave={handleMouseLeave}
        onMouseMove={handleMouseMove}
      >
        {children}
      </div>
      {tooltip && createPortal(tooltip, document.body)}
    </>
  );
}
