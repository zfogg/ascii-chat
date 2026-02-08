import { useEffect, useRef, useState } from 'react'
import 'xterm/css/xterm.css'
import { initMirrorWasm, convertFrameToAscii, isWasmReady, setDimensions, setColorMode, setColorFilter, setPalette, setPaletteChars, setMatrixRain, setWebcamFlip, ColorMode as WasmColorMode, ColorFilter as WasmColorFilter } from '../wasm/mirror'
import { Settings, SettingsConfig, ColorMode, ColorFilter } from '../components/Settings'
import { AsciiRenderer, AsciiRendererHandle } from '../components/AsciiRenderer'
import { WebClientHead } from '../components/WebClientHead'

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
    'yellow': WasmColorFilter.YELLOW,
    'rainbow': WasmColorFilter.RAINBOW
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
  const rendererRef = useRef<AsciiRendererHandle>(null)
  const [isRunning, setIsRunning] = useState(false)
  const [error, setError] = useState<string>('')
  const [terminalDimensions, setTerminalDimensions] = useState({ cols: 0, rows: 0 })
  const streamRef = useRef<MediaStream | null>(null)
  const animationFrameRef = useRef<number | null>(null)
  const lastFrameTimeRef = useRef<number>(0)
  const frameIntervalRef = useRef<number>(1000 / 60)

  // Detect macOS/iOS for webcam flip default
  const isMacOS = /Mac|iPhone|iPad|iPod/.test(navigator.userAgent)

  // Settings state
  const [settings, setSettings] = useState<SettingsConfig>({
    resolution: '640x480',
    targetFps: 60,
    colorMode: 'truecolor',
    colorFilter: 'none',
    palette: 'standard',
    paletteChars: ' =#░░▒▒▓▓██',
    matrixRain: false,
    webcamFlip: isMacOS
  })
  const [showSettings, setShowSettings] = useState(false)

  // Handle settings change
  const handleSettingsChange = (newSettings: SettingsConfig) => {
    setSettings(newSettings)
    frameIntervalRef.current = 1000 / newSettings.targetFps

    if (isWasmReady()) {
      try {
        setColorMode(mapColorMode(newSettings.colorMode))
        setColorFilter(mapColorFilter(newSettings.colorFilter))
        setPalette(newSettings.palette)
        if (newSettings.palette === 'custom' && newSettings.paletteChars) {
          setPaletteChars(newSettings.paletteChars)
        }
        setMatrixRain(newSettings.matrixRain ?? false)
        setWebcamFlip(newSettings.webcamFlip ?? isMacOS)
      } catch (err) {
        console.error('Failed to apply WASM settings:', err)
      }
    }
  }

  // Handle dimension changes from AsciiRenderer
  const handleDimensionsChange = (dims: { cols: number; rows: number }) => {
    setTerminalDimensions(dims)
    if (isWasmReady()) {
      setDimensions(dims.cols, dims.rows)
    }
  }

  // Initialize WASM on mount
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

  const startWebcam = async () => {
    if (!videoRef.current || !canvasRef.current) {
      setError('Video or canvas element not ready')
      return
    }

    const dims = rendererRef.current?.getDimensions()
    if (!dims || dims.cols === 0 || dims.rows === 0) {
      setError('Terminal not ready. Please wait a moment and try again.')
      return
    }

    try {
      // Reapply settings to WASM before starting
      if (isWasmReady()) {
        setColorMode(mapColorMode(settings.colorMode))
        setColorFilter(mapColorFilter(settings.colorFilter))
        setPalette(settings.palette)
        if (settings.palette === 'custom' && settings.paletteChars) {
          setPaletteChars(settings.paletteChars)
        }
        setMatrixRain(settings.matrixRain ?? false)
        setWebcamFlip(settings.webcamFlip ?? isMacOS)
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

    rendererRef.current?.clear()
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
    }

    animationFrameRef.current = requestAnimationFrame(renderLoop)
  }

  const renderFrame = () => {
    const video = videoRef.current
    const canvas = canvasRef.current

    if (!video || !canvas || !isWasmReady() || !rendererRef.current) return

    const ctx = canvas.getContext('2d', { willReadFrequently: true })
    if (!ctx) return

    ctx.drawImage(video, 0, 0, canvas.width, canvas.height)
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height)
    const rgbaData = new Uint8Array(imageData.data)

    const asciiArt = convertFrameToAscii(rgbaData, canvas.width, canvas.height)
    rendererRef.current.writeFrame(asciiArt)
  }

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stopWebcam()
    }
  }, [])

  return (
    <>
      <WebClientHead
        title="Mirror Mode - ascii-chat Web Client"
        description="Test your webcam with real-time ASCII art rendering. See yourself in terminal-style graphics."
        url="https://web.ascii-chat.com/mirror"
      />
      <div className="flex-1 bg-terminal-bg text-terminal-fg flex flex-col">
        {/* Hidden video and canvas for capture */}
        {/*
          The video/canvas container uses position:fixed with 1px dimensions + overflow:hidden
          to keep elements technically "visible" while visually hidden. Using opacity:0 or
          width/height:0 causes browsers to pause invisible videos on scroll.
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
              Settings
            </button>
          </div>
        </div>

        {/* ASCII output via shared renderer */}
        <AsciiRenderer
          ref={rendererRef}
          onDimensionsChange={handleDimensionsChange}
          error={error}
          showFps={isRunning}
        />
      </div>
    </>
  )
}
