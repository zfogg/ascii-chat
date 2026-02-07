import { useEffect, useRef, useState } from 'react'
import { XTerm } from '@pablo-lion/xterm-react'
import { FitAddon } from '@xterm/addon-fit'
import 'xterm/css/xterm.css'
import { initMirrorWasm, convertFrameToAscii, isWasmReady, setDimensions, setColorMode, setColorFilter, setPalette, setPaletteChars, setMatrixRain, ColorMode as WasmColorMode, ColorFilter as WasmColorFilter } from '../wasm/mirror'
import { Settings, SettingsConfig, ColorMode, ColorFilter } from '../components/Settings'

// Helper functions to map Settings types to WASM enums
function mapColorMode(mode: ColorMode): WasmColorMode {
  const mapping: Record<ColorMode, WasmColorMode> = {
    'auto': WasmColorMode.AUTO,
    'none': WasmColorMode.NONE,
    '16': WasmColorMode.COLOR_16,
    '256': WasmColorMode.COLOR_256,
    'truecolor': WasmColorMode.TRUECOLOR
  }
  return mapping[mode]
}

function mapColorFilter(filter: ColorFilter): WasmColorFilter {
  const mapping: Record<ColorFilter, WasmColorFilter> = {
    'none': WasmColorFilter.NONE,
    'black': WasmColorFilter.BLACK,
    'white': WasmColorFilter.WHITE,
    'green': WasmColorFilter.GREEN,
    'magenta': WasmColorFilter.MAGENTA,
    'fuchsia': WasmColorFilter.FUCHSIA,
    'orange': WasmColorFilter.ORANGE,
    'teal': WasmColorFilter.TEAL,
    'cyan': WasmColorFilter.CYAN,
    'pink': WasmColorFilter.PINK,
    'red': WasmColorFilter.RED,
    'yellow': WasmColorFilter.YELLOW
  }
  return mapping[filter]
}

function parseResolution(resolution: string): { width: number; height: number } {
  const [width, height] = resolution.split('x').map(Number)
  return { width, height }
}

export function MirrorPage() {
  const videoRef = useRef<HTMLVideoElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const xtermRef = useRef<any>(null)
  const fitAddonRef = useRef<FitAddon | null>(null)
  const fpsRef = useRef<HTMLDivElement>(null)
  const [isRunning, setIsRunning] = useState(false)
  const [error, setError] = useState<string>('')
  const [terminalDimensions, setTerminalDimensions] = useState({ cols: 0, rows: 0 })
  const terminalDimensionsRef = useRef({ cols: 0, rows: 0 })
  const streamRef = useRef<MediaStream | null>(null)
  const animationFrameRef = useRef<number | null>(null)
  const lastFrameTimeRef = useRef<number>(0)
  const frameCountRef = useRef<number>(0)
  const fpsUpdateTimeRef = useRef<number>(0)
  const setupDoneRef = useRef(false)
  const frameIntervalRef = useRef<number>(1000 / 60) // Default to 60 FPS

  // Settings state
  const [settings, setSettings] = useState<SettingsConfig>({
    resolution: '640x480',
    targetFps: 60,
    colorMode: 'truecolor',
    colorFilter: 'none',
    palette: 'standard',
    paletteChars: ' =#░░▒▒▓▓██',
    matrixRain: false
  })
  const [showSettings, setShowSettings] = useState(false)

  // Handle settings change
  const handleSettingsChange = (newSettings: SettingsConfig) => {
    setSettings(newSettings)

    // Update frame interval
    frameIntervalRef.current = 1000 / newSettings.targetFps

    // Apply settings to WASM if initialized
    if (isWasmReady()) {
      try {
        setColorMode(mapColorMode(newSettings.colorMode))
        setColorFilter(mapColorFilter(newSettings.colorFilter))
        setPalette(newSettings.palette)

        // Apply custom palette characters if palette is custom
        if (newSettings.palette === 'custom' && newSettings.paletteChars) {
          setPaletteChars(newSettings.paletteChars)
        }

        // Apply matrix rain effect
        setMatrixRain(newSettings.matrixRain ?? false)
      } catch (err) {
        console.error('Failed to apply WASM settings:', err)
      }
    }
  }

  // Initialize WASM on mount with default dimensions and settings
  // Dimensions will be updated when terminal is fitted
  useEffect(() => {
    initMirrorWasm({
      width: 80,
      height: 24,
      colorMode: mapColorMode(settings.colorMode),
      colorFilter: mapColorFilter(settings.colorFilter),
      palette: settings.palette
    })
      .catch((err) => {
        console.error('WASM init error:', err)
        setError(`Failed to load WASM module: ${err}`)
      })
  }, [])

  // Ref callback to set up terminal when mounted
  const handleXTermRef = (instance: any) => {
    xtermRef.current = instance

    if (!instance || setupDoneRef.current) return

    // Wait for next tick to ensure terminal is fully initialized
    setTimeout(() => {
      if (!instance.terminal) return

      const terminal = instance.terminal

      const fitAddon = new FitAddon()
      terminal.loadAddon(fitAddon)

      try {
        fitAddon.fit()

        // Get actual dimensions after fitting
        const cols = terminal.cols
        const rows = terminal.rows

        // Update both ref (synchronous) and state (for display)
        terminalDimensionsRef.current = { cols, rows }
        setTerminalDimensions({ cols, rows })

        // Update WASM dimensions immediately (synchronous)
        if (isWasmReady()) {
          setDimensions(cols, rows)
        }
      } catch (e) {
        console.error('[Mirror] FitAddon error:', e)
      }

      fitAddonRef.current = fitAddon

      // FORCE DISABLE IntersectionObserver pause mechanism
      const core = (terminal as any)._core
      if (core) {
        const renderService = core._renderService
        if (renderService) {
          // Override the _handleIntersectionChange to prevent pausing
          const originalHandler = renderService._handleIntersectionChange.bind(renderService)
          renderService._handleIntersectionChange = (entry: any) => {
            originalHandler(entry)
            renderService._isPaused = false
          }
          // Also force it to false immediately
          renderService._isPaused = false
        }
      }

      const handleResize = () => {
        try {
          fitAddon.fit()

          // Update dimensions after resize
          const cols = terminal.cols
          const rows = terminal.rows

          // Update both ref (synchronous) and state (for display)
          terminalDimensionsRef.current = { cols, rows }
          setTerminalDimensions({ cols, rows })

          if (isWasmReady()) {
            setDimensions(cols, rows)
          }
        } catch (e) {
          // Ignore
        }
      }
      window.addEventListener('resize', handleResize)

      setupDoneRef.current = true
    }, 100)
  }

  const startWebcam = async () => {
    if (!videoRef.current || !canvasRef.current) {
      console.error('Video or canvas ref not available')
      setError('Video or canvas element not ready')
      return
    }

    if (!xtermRef.current?.terminal) {
      console.error('Terminal not initialized')
      setError('Terminal not initialized. Please refresh the page.')
      return
    }

    if (terminalDimensions.cols === 0 || terminalDimensions.rows === 0) {
      console.error('Terminal dimensions not set')
      setError('Terminal not ready. Please wait a moment and try again.')
      return
    }

    try {
      // Reapply settings to WASM before starting
      if (isWasmReady()) {
        setColorMode(mapColorMode(settings.colorMode))
        setColorFilter(mapColorFilter(settings.colorFilter))
        setPalette(settings.palette)

        // Apply custom palette characters if palette is custom
        if (settings.palette === 'custom' && settings.paletteChars) {
          setPaletteChars(settings.paletteChars)
        }

        // Apply matrix rain effect
        setMatrixRain(settings.matrixRain ?? false)
      }

      const { width, height } = parseResolution(settings.resolution)
      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: width },
          height: { ideal: height },
          facingMode: 'user',
        },
        audio: false,
      })

      streamRef.current = stream
      videoRef.current.srcObject = stream

      await new Promise<void>((resolve) => {
        videoRef.current!.addEventListener('loadedmetadata', () => {
          const video = videoRef.current!
          const canvas = canvasRef.current!
          canvas.width = video.videoWidth
          canvas.height = video.videoHeight
          resolve()
        }, { once: true })
      })

      setIsRunning(true)
      lastFrameTimeRef.current = performance.now()
      fpsUpdateTimeRef.current = lastFrameTimeRef.current
      frameCountRef.current = 0
      renderLoop()
    } catch (err) {
      setError(`Failed to start webcam: ${err}`)
    }
  }

  const stopWebcam = () => {
    if (animationFrameRef.current !== null) {
      cancelAnimationFrame(animationFrameRef.current)
      animationFrameRef.current = null
    }

    if (streamRef.current) {
      streamRef.current.getTracks().forEach(track => track.stop())
      streamRef.current = null
    }

    if (videoRef.current) {
      videoRef.current.srcObject = null
    }

    if (xtermRef.current?.terminal) {
      xtermRef.current.terminal.clear()
    }

    if (fpsRef.current) {
      fpsRef.current.textContent = '--'
    }
    setIsRunning(false)
  }

  const renderLoop = () => {
    const now = performance.now()
    const elapsed = now - lastFrameTimeRef.current

    if (elapsed >= frameIntervalRef.current) {
      lastFrameTimeRef.current = now

      try {
        renderFrame()
      } catch (err) {
        console.error('[renderLoop] Frame render error:', err)
        setError(`Render error: ${err}`)
        stopWebcam()
        return
      }

      // Update FPS counter every second - direct DOM update to avoid re-renders
      frameCountRef.current++
      if (now - fpsUpdateTimeRef.current >= 1000) {
        const currentFps = Math.round(frameCountRef.current / ((now - fpsUpdateTimeRef.current) / 1000))
        if (fpsRef.current) {
          fpsRef.current.textContent = currentFps.toString()
        }
        frameCountRef.current = 0
        fpsUpdateTimeRef.current = now
      }
    }

    animationFrameRef.current = requestAnimationFrame(renderLoop)
  }

  const renderFrame = () => {
    const video = videoRef.current
    const canvas = canvasRef.current
    const terminal = xtermRef.current?.terminal

    if (!video || !canvas || !terminal || !isWasmReady()) {
      console.log('[renderFrame] Early return:', { video: !!video, canvas: !!canvas, terminal: !!terminal, wasmReady: isWasmReady() })
      return
    }

    // Skip frame if terminal dimensions don't match WASM dimensions (use ref for synchronous check)
    if (terminal.cols !== terminalDimensionsRef.current.cols || terminal.rows !== terminalDimensionsRef.current.rows) {
      console.log('[renderFrame] Dimension mismatch, skipping frame:', { termCols: terminal.cols, termRows: terminal.rows, refCols: terminalDimensionsRef.current.cols, refRows: terminalDimensionsRef.current.rows })
      return // Dimensions are being updated, skip this frame
    }

    const ctx = canvas.getContext('2d', { willReadFrequently: true })
    if (!ctx) return

    // Draw video frame to canvas
    ctx.drawImage(video, 0, 0, canvas.width, canvas.height)

    // Get RGBA pixel data
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height)
    const rgbaData = new Uint8Array(imageData.data)

    // Convert to ASCII using WASM
    const asciiArt = convertFrameToAscii(
      rgbaData,
      canvas.width,
      canvas.height
    )

    // Add proper terminal line endings (\r\n)
    const lines = asciiArt.split('\n')
    const formattedLines = lines.map((line, index) =>
      index < lines.length - 1 ? line + '\r\n' : line
    )

    // WASM output already includes ANSI color codes
    // Use cursor home + clear screen to prevent artifacts
    const output = '\x1b[H\x1b[J' + formattedLines.join('')

    // Debug: log every 30 frames to track rendering
    if (frameCountRef.current % 30 === 0) {
      const core = (terminal as any)._core
      const renderService = core?._renderService
      const isPaused = renderService?._isPaused
      console.log('[renderFrame] Lines:', lines.length, 'Output length:', output.length, 'ASCII length:', asciiArt.length, 'isPaused:', isPaused)
    }

    // Write output (contains \x1b[H\x1b[J for cursor home + clear screen)
    terminal.write(output)
  }

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stopWebcam()
    }
  }, [])

  return (
    <div className="flex-1 bg-terminal-bg text-terminal-fg flex flex-col">
      {/* Hidden video and canvas for capture - visually hidden but active */}
      {/*
        NOTE: The video/canvas container MUST be visible to the browser (not width:0/height:0 or opacity:0)
        to prevent the browser from pausing video playback when the page scrolls. Using position:fixed with
        1px×1px + overflow:hidden keeps the elements technically "visible" while visually hiding them.
        Using opacity:0 or width/height:0 causes browsers to optimize by pausing invisible videos on scroll.
      */}
      <div style={{ position: 'fixed', bottom: 0, right: 0, width: '1px', height: '1px', overflow: 'hidden', pointerEvents: 'none' }}>
        <video ref={videoRef} autoPlay muted playsInline style={{ width: '640px', height: '480px' }} />
        <canvas ref={canvasRef} />
      </div>

      {/* Settings Panel */}
      {showSettings && (
        <Settings
          config={settings}
          onChange={handleSettingsChange}
        />
      )}

      {/* Controls and info */}
      <div className="px-4 py-3 flex-shrink-0 border-b border-terminal-8">
        <div className="flex items-center justify-between mb-2">
          <h2 className="text-sm font-semibold">
            ASCII Mirror {terminalDimensions.cols > 0 && `(${terminalDimensions.cols}x${terminalDimensions.rows})`}
          </h2>
          <div className="text-xs text-terminal-8">
            FPS: <span className="text-terminal-2" ref={fpsRef}>--</span>
          </div>
        </div>
        <div className="flex gap-2">
          {!isRunning ? (
            <button
              onClick={startWebcam}
              className="px-4 py-2 bg-terminal-2 text-terminal-bg rounded hover:bg-terminal-10"
            >
              Start Webcam
            </button>
          ) : (
            <button
              onClick={stopWebcam}
              className="px-4 py-2 bg-terminal-1 text-terminal-bg rounded hover:bg-terminal-9"
            >
              Stop
            </button>
          )}
          <button
            onClick={() => setShowSettings(!showSettings)}
            className="px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7"
            title="Settings"
          >
            ⚙️ Settings
          </button>
        </div>
      </div>

      {/* ASCII output */}
      <div className="flex-1 px-4 py-2 overflow-hidden" style={{ pointerEvents: 'none', display: 'flex', flexDirection: 'column' }}>
        <style>{`
          .xterm {
            flex: 1 !important;
          }
        `}</style>
        <XTerm
          ref={handleXTermRef}
          options={{
            // Let FitAddon calculate cols/rows based on container size
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

      {error && (
        <div className="px-4 pb-2">
          <div className="p-4 bg-terminal-1 text-terminal-fg rounded">
            {error}
          </div>
        </div>
      )}
    </div>
  )
}
