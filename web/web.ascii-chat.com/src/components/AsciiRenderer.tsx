import {
  forwardRef,
  useCallback,
  useEffect,
  useImperativeHandle,
  useRef,
} from "react";
import type { Terminal } from "xterm";
import { XTerm, type XTerm as XTermType } from "@pablo-lion/xterm-react";
import { FitAddon } from "@xterm/addon-fit";
import "xterm/css/xterm.css";

export interface AsciiRendererHandle {
  writeFrame(ansiString: string): void;
  getDimensions(): { cols: number; rows: number };
  clear(): void;
}

interface XTermWithElement extends XTermType {
  element?: HTMLElement;
}

export interface AsciiRendererProps {
  onDimensionsChange?: (dims: { cols: number; rows: number }) => void;
  onFpsChange?: (fps: number) => void;
  error?: string;
  showFps?: boolean;
}

export const AsciiRenderer = forwardRef<
  AsciiRendererHandle,
  AsciiRendererProps
>(function AsciiRenderer(
  { onDimensionsChange, onFpsChange, error, showFps = true },
  ref,
) {
  const xtermRef = useRef<XTermType | null>(null);
  const fitAddonRef = useRef<FitAddon | null>(null);
  const fpsRef = useRef<HTMLDivElement>(null);
  const setupDoneRef = useRef(false);
  const dimensionsRef = useRef({ cols: 0, rows: 0 });

  // FPS tracking via direct DOM updates
  const frameCountRef = useRef(0);
  const fpsUpdateTimeRef = useRef<number | null>(null);
  const lastDimsRef = useRef({ cols: 0, rows: 0 });
  const pendingFrameRef = useRef<string | null>(null);
  const rafIdRef = useRef<number | null>(null);

  // Initialize FPS timer
  useEffect(() => {
    fpsUpdateTimeRef.current = performance.now();
  }, []);

  const updateDimensions = useCallback(
    (cols: number, rows: number) => {
      // If dimensions changed, clear the terminal to prevent leftover ASCII art
      if (
        dimensionsRef.current.cols !== cols ||
        dimensionsRef.current.rows !== rows
      ) {
        const xterm = xtermRef.current;
        if (xterm) {
          const terminal = (xterm as XTermType & { terminal: Terminal })
            .terminal;
          if (terminal) {
            terminal.write("\x1b[H\x1b[J"); // cursor home + clear screen
          }
        }
      }

      dimensionsRef.current = { cols, rows };
      onDimensionsChange?.({ cols, rows });
    },
    [onDimensionsChange],
  );

  useImperativeHandle(
    ref,
    () => ({
      writeFrame(ansiString: string) {
        // Queue latest frame
        pendingFrameRef.current = ansiString;

        // Cancel previous RAF if pending
        if (rafIdRef.current !== null) {
          cancelAnimationFrame(rafIdRef.current);
        }

        // Schedule render on next animation frame
        rafIdRef.current = requestAnimationFrame(() => {
          const xterm = xtermRef.current;
          if (!xterm || !pendingFrameRef.current) return;

          const terminal = (xterm as XTermType & { terminal: Terminal })
            .terminal;
          if (!terminal) return;

          const ansiString = pendingFrameRef.current;

          const lines = ansiString.split("\n");

          const formattedLines = lines.map((line: string, index: number) =>
            index < lines.length - 1 ? line + "\r\n" : line,
          );

          // Use cursor home only. Clear screen only when dimensions changed.
          const dims = dimensionsRef.current;
          let prefix = "\x1b[H";
          if (
            lastDimsRef.current.cols !== dims.cols ||
            lastDimsRef.current.rows !== dims.rows
          ) {
            prefix = "\x1b[H\x1b[J";
            lastDimsRef.current = { ...dims };
            console.log(
              `[AsciiRenderer] Dimensions changed, clearing screen: ${dims.cols}x${dims.rows}`,
            );
          }

          const output = prefix + formattedLines.join("");

          terminal.write(output);

          // Ensure terminal is not paused and will render
          const core = (
            terminal as Terminal & {
              _core?: {
                _renderService?: {
                  _isPaused: boolean;
                  _renderRows?: (start: number, end: number) => void;
                };
              };
            }
          )._core;
          if (core && core._renderService) {
            const renderService = core._renderService;

            // Force unpause - this is critical for continuous rendering
            renderService._isPaused = false;

            // Call _renderRows to force immediate render of the updated content
            // This is the actual render method in xterm 5.3.0
            if (renderService._renderRows) {
              try {
                renderService._renderRows(0, terminal.rows);
              } catch (e) {
                console.error(
                  `[AsciiRenderer] Error calling _renderRows: ${e}`,
                );
              }
            }
          }

          // Update FPS counter via direct DOM mutation and callback
          if (showFps && fpsUpdateTimeRef.current !== null) {
            frameCountRef.current++;
            const now = performance.now();
            const elapsed = now - fpsUpdateTimeRef.current;
            if (elapsed >= 1000) {
              const fps = Math.round(frameCountRef.current / (elapsed / 1000));
              if (fpsRef.current) {
                fpsRef.current.textContent = fps.toString();
              }
              onFpsChange?.(fps);
              frameCountRef.current = 0;
              fpsUpdateTimeRef.current = now;
            }
          }

          rafIdRef.current = null;
        });
      },

      getDimensions() {
        return dimensionsRef.current;
      },

      clear() {
        const xterm = xtermRef.current;
        if (xterm) {
          const terminal = (xterm as XTermType & { terminal: Terminal })
            .terminal;
          if (terminal) {
            console.log("[AsciiRenderer] clear()");
            terminal.clear();
          }
        }
      },
    }),
    [showFps, onFpsChange],
  );

  const handleXTermRef = useCallback(
    (instance: XTermType | null) => {
      console.log("[AsciiRenderer] handleXTermRef called");
      xtermRef.current = instance;

      if (!instance) return;

      if (setupDoneRef.current) {
        console.log("[AsciiRenderer] setup already done, skipping");
        return;
      }

      console.log("[AsciiRenderer] Scheduling xterm setup...");
      setTimeout(() => {
        console.log("[AsciiRenderer] Setup timeout triggered");
        if (!instance) return;

        const terminal = (instance as XTermType & { terminal: Terminal })
          .terminal;
        if (!terminal) {
          console.error("[AsciiRenderer] No terminal instance found");
          return;
        }

        console.log("[AsciiRenderer] Terminal instance ready, setting up");
        const fitAddon = new FitAddon();
        console.log("[AsciiRenderer] Loading FitAddon");
        terminal.loadAddon(fitAddon);

        fitAddonRef.current = fitAddon;

        // Disable IntersectionObserver pause mechanism
        console.log(
          "[AsciiRenderer] Disabling IntersectionObserver pause mechanism",
        );
        const core = (
          terminal as Terminal & {
            _core?: {
              _renderService?: {
                _handleIntersectionChange: {
                  bind: (
                    context: unknown,
                  ) => (entry: IntersectionObserverEntry) => void;
                };
                _isPaused: boolean;
              };
            };
          }
        )._core;
        if (core) {
          const renderService = core._renderService;
          if (renderService) {
            const originalHandler =
              renderService._handleIntersectionChange.bind(renderService);
            renderService._handleIntersectionChange = (
              entry: IntersectionObserverEntry,
            ) => {
              originalHandler(entry);
              renderService._isPaused = false;
            };
            renderService._isPaused = false;
            console.log(
              "[AsciiRenderer] IntersectionObserver override applied",
            );
          }
        }

        // Define resize handler used for both initial sizing and window resize
        const handleResize = () => {
          try {
            console.log("[AsciiRenderer] Resizing terminal");
            fitAddon.fit();
            console.log(
              `[AsciiRenderer] FitAddon fit complete: ${terminal.cols}x${terminal.rows}`,
            );
            updateDimensions(terminal.cols, terminal.rows);
          } catch (e) {
            console.error("[AsciiRenderer] Resize error:", e);
          }
        };

        // Use ResizeObserver to detect when the xterm container has actual dimensions
        const resizeObserver = new ResizeObserver(() => {
          console.log(
            "[AsciiRenderer] Container resized, applying initial fit",
          );
          handleResize();
          // Only need initial fit once, then listen to window resize
          resizeObserver.disconnect();
        });

        // Observe the xterm viewport to know when container is ready
        const xtermViewport = (
          instance as XTermWithElement
        ).element?.querySelector(".xterm-viewport");
        if (xtermViewport) {
          resizeObserver.observe(xtermViewport);
        } else {
          console.warn("[AsciiRenderer] Could not find xterm-viewport");
          handleResize();
        }

        // Listen for future window resize events
        window.addEventListener("resize", handleResize);
        console.log("[AsciiRenderer] Resize event listener added");

        console.log("[AsciiRenderer] Setup complete");
        setupDoneRef.current = true;
      }, 100);
    },
    [updateDimensions],
  );

  return (
    <>
      {/* ASCII terminal output */}
      <div className="flex flex-col flex-1 px-4 py-2 overflow-hidden min-h-0">
        <style>
          {`
            .xterm {
              flex: 1 !important;
              min-height: 0;
              width: 100%;
            }
            .xterm-viewport {
              overflow-y: hidden !important;
              overflow-x: hidden !important;
            }
          `}
        </style>
        <XTerm
          ref={handleXTermRef}
          options={{
            theme: {
              background: "#0c0c0c",
              foreground: "#cccccc",
            },
            cursorStyle: "block",
            cursorBlink: false,
            fontFamily: '"FiraCode Nerd Font Mono", "Fira Code", monospace',
            fontSize: 12,
            scrollback: 0,
            disableStdin: true,
            allowTransparency: false,
            convertEol: false,
            drawBoldTextInBrightColors: true,
          }}
          className="flex flex-1 rounded bg-terminal-bg"
        />
      </div>

      {/* FPS counter - hidden, displayed in control bar instead */}
      {showFps && (
        <div ref={fpsRef} style={{ display: "none" }}>
          --
        </div>
      )}

      {/* Error bar */}
      {error && (
        <div className="px-4 pb-2">
          <div className="p-4 bg-terminal-1 text-terminal-fg rounded">
            {error}
          </div>
        </div>
      )}
    </>
  );
});
