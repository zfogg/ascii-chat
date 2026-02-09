import { forwardRef, useImperativeHandle, useRef, useCallback } from 'react'
import { XTerm } from '@pablo-lion/xterm-react'
import { FitAddon } from '@xterm/addon-fit'
import 'xterm/css/xterm.css'

export interface AsciiRendererHandle {
  writeFrame(ansiString: string): void
  getDimensions(): { cols: number; rows: number }
  clear(): void
}

export interface AsciiRendererProps {
  onDimensionsChange?: (dims: { cols: number; rows: number }) => void
  error?: string
  showFps?: boolean
}

export const AsciiRenderer = forwardRef<AsciiRendererHandle, AsciiRendererProps>(
  function AsciiRenderer({ onDimensionsChange, error, showFps = true }, ref) {
    const xtermRef = useRef<any>(null)
    const fitAddonRef = useRef<FitAddon | null>(null)
    const fpsRef = useRef<HTMLDivElement>(null)
    const setupDoneRef = useRef(false)
    const dimensionsRef = useRef({ cols: 0, rows: 0 })

    // FPS tracking via direct DOM updates
    const frameCountRef = useRef(0)
    const fpsUpdateTimeRef = useRef(performance.now())
    const lastDimsRef = useRef({ cols: 0, rows: 0 })

    const updateDimensions = useCallback((cols: number, rows: number) => {
      console.log(`[AsciiRenderer] updateDimensions: ${cols}x${rows}`)
      dimensionsRef.current = { cols, rows }
      onDimensionsChange?.({ cols, rows })
    }, [onDimensionsChange])

    useImperativeHandle(ref, () => ({
      writeFrame(ansiString: string) {
        const terminal = xtermRef.current?.terminal
        if (!terminal) {
          console.log('[AsciiRenderer] writeFrame: terminal not ready')
          return
        }

        console.log(`[AsciiRenderer] ========== WRITE FRAME ==========`)
        console.log(`[AsciiRenderer] Input ANSI string: ${ansiString.length} chars`)

        const lines = ansiString.split('\n')
        console.log(`[AsciiRenderer] Lines in input: ${lines.length}`)
        console.log(`[AsciiRenderer] First 3 lines (first 80 chars each):`)
        lines.slice(0, 3).forEach((line, i) => {
          console.log(`[AsciiRenderer]   Line ${i}: "${line.substring(0, 80)}"`)
        })

        const formattedLines = lines.map((line: string, index: number) =>
          index < lines.length - 1 ? line + '\r\n' : line
        )

        // Use cursor home only. Clear screen only when dimensions changed.
        const dims = dimensionsRef.current
        let prefix = '\x1b[H'
        if (lastDimsRef.current.cols !== dims.cols || lastDimsRef.current.rows !== dims.rows) {
          prefix = '\x1b[H\x1b[J'
          lastDimsRef.current = { ...dims }
          console.log(`[AsciiRenderer] Dimensions changed, clearing screen: ${dims.cols}x${dims.rows}`)
        }

        const output = prefix + formattedLines.join('')
        console.log(`[AsciiRenderer] Output to terminal: ${output.length} chars`)
        console.log(`[AsciiRenderer] Prefix bytes: ${Array.from(prefix).map(c => '0x' + c.charCodeAt(0).toString(16)).join(' ')}`)
        console.log(`[AsciiRenderer] ========== WRITING TO XTERM ==========`)

        terminal.write(output)
        console.log(`[AsciiRenderer] Frame written to xterm`)

        // Ensure terminal is not paused and will render
        const core = (terminal as any)._core
        if (core && core._renderService) {
          const renderService = core._renderService
          const wasPaused = renderService._isPaused
          console.log(`[AsciiRenderer] ===== RENDER SERVICE STATE =====`)
          console.log(`[AsciiRenderer] isPaused BEFORE: ${wasPaused}`)

          // Force unpause - this is the critical step
          renderService._isPaused = false
          console.log(`[AsciiRenderer] isPaused AFTER setting to false: ${renderService._isPaused}`)

          // Try multiple approaches to ensure rendering
          // 1. Try calling refresh/render methods if they exist
          if (renderService.refresh) {
            renderService.refresh()
            console.log(`[AsciiRenderer] Called renderService.refresh()`)
          }
          if ((renderService as any).markDirty) {
            (renderService as any).markDirty()
            console.log(`[AsciiRenderer] Called renderService.markDirty()`)
          }

          // 2. Try direct render method if it exists
          const renderMethods = Object.getOwnPropertyNames(Object.getPrototypeOf(renderService))
            .filter(m => m.toLowerCase().includes('render') || m.toLowerCase().includes('draw'))
          console.log(`[AsciiRenderer] Available render methods: ${renderMethods.join(', ')}`)
        } else {
          console.error(`[AsciiRenderer] ERROR: Cannot access render service. core=${!!core}, _renderService=${core?._renderService}`)
        }

        // Update FPS counter via direct DOM mutation
        if (showFps && fpsRef.current) {
          frameCountRef.current++
          const now = performance.now()
          if (now - fpsUpdateTimeRef.current >= 1000) {
            const fps = Math.round(frameCountRef.current / ((now - fpsUpdateTimeRef.current) / 1000))
            fpsRef.current.textContent = fps.toString()
            frameCountRef.current = 0
            fpsUpdateTimeRef.current = now
          }
        }
      },

      getDimensions() {
        const dims = dimensionsRef.current
        console.log(`[AsciiRenderer] getDimensions: ${dims.cols}x${dims.rows}`)
        return dims
      },

      clear() {
        const terminal = xtermRef.current?.terminal
        if (terminal) {
          console.log('[AsciiRenderer] clear()')
          terminal.clear()
        } else {
          console.log('[AsciiRenderer] clear() - terminal not available')
        }
      }
    }), [showFps])

    const handleXTermRef = useCallback((instance: any) => {
      console.log('[AsciiRenderer] handleXTermRef called')
      xtermRef.current = instance

      if (!instance) {
        console.log('[AsciiRenderer] instance is null, skipping setup')
        return
      }

      if (setupDoneRef.current) {
        console.log('[AsciiRenderer] setup already done, skipping')
        return
      }

      console.log('[AsciiRenderer] Scheduling xterm setup...')
      setTimeout(() => {
        console.log('[AsciiRenderer] Setup timeout triggered')
        if (!instance.terminal) {
          console.log('[AsciiRenderer] terminal not available yet')
          return
        }

        console.log('[AsciiRenderer] Terminal instance ready, setting up')
        const terminal = instance.terminal
        const fitAddon = new FitAddon()
        console.log('[AsciiRenderer] Loading FitAddon')
        terminal.loadAddon(fitAddon)

        try {
          console.log('[AsciiRenderer] Calling fitAddon.fit()')
          fitAddon.fit()
          console.log(`[AsciiRenderer] FitAddon fit complete: ${terminal.cols}x${terminal.rows}`)
          updateDimensions(terminal.cols, terminal.rows)
        } catch (e) {
          console.error('[AsciiRenderer] FitAddon error:', e)
        }

        fitAddonRef.current = fitAddon

        // Disable IntersectionObserver pause mechanism
        console.log('[AsciiRenderer] Disabling IntersectionObserver pause mechanism')
        const core = (terminal as any)._core
        if (core) {
          const renderService = core._renderService
          if (renderService) {
            const originalHandler = renderService._handleIntersectionChange.bind(renderService)
            renderService._handleIntersectionChange = (entry: any) => {
              originalHandler(entry)
              renderService._isPaused = false
            }
            renderService._isPaused = false
            console.log('[AsciiRenderer] IntersectionObserver override applied')
          }
        }

        const handleResize = () => {
          try {
            console.log('[AsciiRenderer] Window resize event')
            fitAddon.fit()
            updateDimensions(terminal.cols, terminal.rows)
          } catch (e) {
            console.error('[AsciiRenderer] Resize error:', e)
          }
        }
        window.addEventListener('resize', handleResize)
        console.log('[AsciiRenderer] Resize event listener added')

        console.log('[AsciiRenderer] Setup complete')
        setupDoneRef.current = true
      }, 100)
    }, [updateDimensions])

    return (
      <>
        {/* ASCII terminal output */}
        <div className="flex-1 px-4 py-2 overflow-hidden min-h-0" style={{ pointerEvents: 'none', display: 'flex', flexDirection: 'column' }}>
          <style>{`
            .xterm {
              flex: 1 !important;
              min-height: 0;
              width: 100%;
            }
            .xterm-viewport {
              overflow-y: hidden !important;
              overflow-x: hidden !important;
            }
          `}</style>
          <XTerm
            ref={handleXTermRef}
            options={{
              theme: {
                background: '#0c0c0c',
                foreground: '#cccccc',
              },
              cursorStyle: 'block',
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

        {/* FPS counter */}
        {showFps && (
          <div className="text-xs text-terminal-8 absolute top-0 right-0 px-2 py-1" style={{ pointerEvents: 'none' }}>
            FPS: <span className="text-terminal-2" ref={fpsRef}>--</span>
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
    )
  }
)
