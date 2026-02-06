import { useEffect, useRef, useState } from 'react'
import { Terminal } from 'xterm'
import 'xterm/css/xterm.css'
import { initMirrorWasm, convertFrameToAscii, isWasmReady } from '../wasm/mirror'

// Configuration
const ASCII_WIDTH = 80
const ASCII_HEIGHT = 24
const TARGET_FPS = 30
const FRAME_INTERVAL = 1000 / TARGET_FPS

export function MirrorPage() {
  const videoRef = useRef<HTMLVideoElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const terminalRef = useRef<HTMLDivElement>(null)
  const xtermRef = useRef<Terminal | null>(null)
  const [fps, setFps] = useState<string>('--')
  const [isRunning, setIsRunning] = useState(false)
  const [error, setError] = useState<string>('')
  const streamRef = useRef<MediaStream | null>(null)
  const animationFrameRef = useRef<number | null>(null)
  const lastFrameTimeRef = useRef<number>(0)
  const frameCountRef = useRef<number>(0)
  const fpsUpdateTimeRef = useRef<number>(0)

  // Initialize WASM on mount
  useEffect(() => {
    console.log('Initializing WASM module...')
    initMirrorWasm()
      .then(() => console.log('WASM module loaded successfully'))
      .catch((err) => {
        console.error('WASM init error:', err)
        setError(`Failed to load WASM module: ${err}`)
      })
  }, [])

  // Initialize xterm.js
  useEffect(() => {
    if (!terminalRef.current) {
      console.error('Terminal ref not available')
      return
    }

    console.log('Initializing xterm.js terminal...')
    try {
      const terminal = new Terminal({
        cols: ASCII_WIDTH,
        rows: ASCII_HEIGHT,
        theme: {
          background: '#0c0c0c',
          foreground: '#cccccc',
        },
        cursorStyle: 'bar',
        cursorBlink: false,
        fontFamily: 'monospace',
        fontSize: 14,
        scrollback: 0,
        disableStdin: true,
      })

      terminal.open(terminalRef.current)
      xtermRef.current = terminal

      console.log('xterm.js terminal initialized successfully')

      return () => {
        terminal.dispose()
      }
    } catch (err) {
      console.error('Failed to initialize terminal:', err)
      setError(`Failed to initialize terminal: ${err}`)
    }
  }, [])

  const startWebcam = async () => {
    if (!videoRef.current || !canvasRef.current) {
      console.error('Video or canvas ref not available')
      setError('Video or canvas element not ready')
      return
    }

    if (!xtermRef.current) {
      console.error('Terminal not initialized')
      setError('Terminal not initialized. Please refresh the page.')
      return
    }

    console.log('Requesting webcam access...')
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: 640 },
          height: { ideal: 480 },
          facingMode: 'user',
        },
        audio: false,
      })
      console.log('Webcam access granted')

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

    if (xtermRef.current) {
      xtermRef.current.clear()
    }

    setFps('--')
    setIsRunning(false)
  }

  const renderLoop = () => {
    const now = performance.now()
    const elapsed = now - lastFrameTimeRef.current

    if (elapsed >= FRAME_INTERVAL) {
      lastFrameTimeRef.current = now
      renderFrame()

      // Update FPS counter every second
      frameCountRef.current++
      if (now - fpsUpdateTimeRef.current >= 1000) {
        const currentFps = Math.round(frameCountRef.current / ((now - fpsUpdateTimeRef.current) / 1000))
        setFps(currentFps.toString())
        frameCountRef.current = 0
        fpsUpdateTimeRef.current = now
      }
    }

    animationFrameRef.current = requestAnimationFrame(renderLoop)
  }

  const renderFrame = () => {
    const video = videoRef.current
    const canvas = canvasRef.current
    const terminal = xtermRef.current
    if (!video || !canvas || !terminal || !isWasmReady()) return

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
      canvas.height,
      ASCII_WIDTH,
      ASCII_HEIGHT
    )

    // Clear terminal and write ASCII art
    terminal.clear()
    for (let i = 0; i < ASCII_HEIGHT; i++) {
      const line = asciiArt.substring(i * ASCII_WIDTH, (i + 1) * ASCII_WIDTH)
      terminal.writeln(line)
    }
  }

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stopWebcam()
    }
  }, [])

  return (
    <div className="min-h-screen bg-terminal-bg text-terminal-fg p-4">
      <div className="max-w-6xl mx-auto">
        <header className="mb-4">
          <h1 className="text-2xl font-bold mb-2">ascii-chat Mirror Mode</h1>
          <p className="text-terminal-8">Local webcam ASCII preview (no networking)</p>
        </header>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
          {/* Video preview */}
          <div className="bg-terminal-0 p-4 rounded">
            <h2 className="text-lg font-semibold mb-2">Video Input</h2>
            <video
              ref={videoRef}
              className="w-full rounded"
              autoPlay
              muted
              playsInline
            />
            <canvas ref={canvasRef} className="hidden" />
          </div>

          {/* ASCII output */}
          <div className="bg-terminal-0 p-4 rounded">
            <h2 className="text-lg font-semibold mb-2">ASCII Output</h2>
            <div className="mb-2 text-sm text-terminal-8">
              FPS: <span className="text-terminal-2">{fps}</span>
            </div>
            <div ref={terminalRef} className="rounded overflow-hidden" />
          </div>
        </div>

        <div className="mt-4 flex gap-2">
          {!isRunning ? (
            <button
              onClick={() => {
                console.log('Start Webcam button clicked')
                startWebcam()
              }}
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
          <a
            href="/"
            className="px-4 py-2 bg-terminal-8 text-terminal-bg rounded hover:bg-terminal-7 inline-block"
          >
            Back to Home
          </a>
        </div>

        {error && (
          <div className="mt-4 p-4 bg-terminal-1 text-terminal-fg rounded">
            {error}
          </div>
        )}
      </div>
    </div>
  )
}
