import React, {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import { createPortal } from "react-dom";

interface TooltipProps {
  text?: string | undefined;
  children: React.ReactNode;
  className?: string;
  id?: string;
  inline?: boolean;
  showOnFocus?: boolean;
}

export function Tooltip({
  text,
  children,
  className = "flex-1",
  id,
  inline = false,
  showOnFocus = false,
}: TooltipProps) {
  const [visible, setVisible] = useState(false);
  const [position, setPosition] = useState({ left: 8, top: 8 });
  const triggerRef = useRef<HTMLElement>(null);
  const tooltipRef = useRef<HTMLDivElement>(null);
  const pointRef = useRef<{ x: number; y: number } | null>(null);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const focusedRef = useRef(false);

  const cancelTimer = useCallback(() => {
    if (timerRef.current !== null) clearTimeout(timerRef.current);
    timerRef.current = null;
  }, []);
  useEffect(() => cancelTimer, [cancelTimer]);

  const reposition = useCallback(() => {
    const tooltip = tooltipRef.current;
    const trigger = triggerRef.current;
    if (!tooltip || !trigger) return;
    const rect = trigger.getBoundingClientRect();
    const point = pointRef.current ?? { x: rect.left, y: rect.bottom };
    const bounds = tooltip.getBoundingClientRect();
    const margin = 8;
    const offset = 12;
    const left =
      point.x + offset + bounds.width <= window.innerWidth - margin
        ? point.x + offset
        : point.x - offset - bounds.width;
    const top =
      point.y + offset + bounds.height <= window.innerHeight - margin
        ? point.y + offset
        : point.y - offset - bounds.height;
    setPosition({
      left: Math.max(
        margin,
        Math.min(left, window.innerWidth - margin - bounds.width),
      ),
      top: Math.max(
        margin,
        Math.min(top, window.innerHeight - margin - bounds.height),
      ),
    });
  }, []);

  useLayoutEffect(() => {
    if (!visible) return;
    reposition();
    window.addEventListener("resize", reposition);
    window.addEventListener("scroll", reposition, true);
    return () => {
      window.removeEventListener("resize", reposition);
      window.removeEventListener("scroll", reposition, true);
    };
  }, [visible, text, reposition]);

  if (!text) return <>{children}</>;
  const handlers = {
    onMouseEnter: (event: React.MouseEvent) => {
      pointRef.current = { x: event.clientX, y: event.clientY };
      cancelTimer();
      timerRef.current = setTimeout(() => setVisible(true), 350);
    },
    onMouseMove: (event: React.MouseEvent) => {
      pointRef.current = { x: event.clientX, y: event.clientY };
      if (visible) reposition();
    },
    onMouseLeave: () => {
      cancelTimer();
      if (!focusedRef.current)
        timerRef.current = setTimeout(() => setVisible(false), 150);
    },
    onFocusCapture: () => {
      cancelTimer();
      focusedRef.current = showOnFocus;
      if (showOnFocus) pointRef.current = null;
      setVisible(showOnFocus);
    },
    onBlurCapture: () => {
      focusedRef.current = false;
      setVisible(false);
    },
  };
  const trigger = inline ? (
    <span
      ref={triggerRef as React.RefObject<HTMLSpanElement>}
      className={className}
      {...handlers}
    >
      {children}
    </span>
  ) : (
    <div
      ref={triggerRef as React.RefObject<HTMLDivElement>}
      className={className}
      {...handlers}
    >
      {children}
    </div>
  );
  return (
    <>
      {trigger}
      {(visible || id) &&
        createPortal(
          <div
            id={id}
            ref={tooltipRef}
            role="tooltip"
            onMouseEnter={cancelTimer}
            onMouseLeave={() => {
              if (!focusedRef.current) setVisible(false);
            }}
            className="fixed z-50 whitespace-normal break-words rounded bg-gray-800 p-2 text-xs font-normal text-white shadow-lg"
            style={{
              left: position.left,
              top: position.top,
              width: "min(20rem, calc(100vw - 16px))",
              maxHeight: "calc(100vh - 16px)",
              overflow: "auto",
              visibility: visible ? "visible" : "hidden",
            }}
          >
            {text}
          </div>,
          document.body,
        )}
    </>
  );
}
