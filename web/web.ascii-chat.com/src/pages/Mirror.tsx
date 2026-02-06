import { useEffect, useRef, useState } from 'react'
import { Terminal } from 'xterm'
import { FitAddon } from '@xterm/addon-fit'
import 'xterm/css/xterm.css'
import { initMirrorWasm, convertFrameToAscii, isWasmReady } from '../wasm/mirror'

// Configuration
const TARGET_FPS = 60
const FRAME_INTERVAL = 1000 / TARGET_FPS

// Large ASCII resolution - FitAddon will scale font to fill container
// Slightly reduced width to account for container constraints
const ASCII_WIDTH = 150
const ASCII_HEIGHT = 60

export function MirrorPage() {
  console.log('[MirrorPage] Component rendering')

  const videoRef = useRef<HTMLVideoElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const terminalRef = useRef<HTMLDivElement>(null)
  const xtermRef = useRef<Terminal | null>(null)
  const fitAddonRef = useRef<FitAddon | null>(null)
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
    console.log('[Terminal Init] useEffect fired')
    console.log('[Terminal Init] terminalRef.current:', terminalRef.current)

    if (!terminalRef.current) {
      console.error('[Terminal Init] Terminal ref not available')
      return
    }

    console.log('[Terminal Init] Starting initialization...')

    // Wait for next frame to ensure DOM is ready
    const timeoutId = setTimeout(() => {
      console.log('[Terminal Init] setTimeout callback fired')
      try {
        const terminal = new Terminal({
          cols: ASCII_WIDTH,
          rows: ASCII_HEIGHT,
          theme: {
            background: '#0c0c0c',
            foreground: '#cccccc',
          },
          cursorStyle: 'block',
          cursorBlink: false,
          fontFamily: '"Courier New", Courier, monospace',
          fontSize: 12,
          scrollback: 0,
          disableStdin: true,
        })

        const fitAddon = new FitAddon()
        terminal.loadAddon(fitAddon)

        if (terminalRef.current) {
          terminal.open(terminalRef.current)

          // Skip initial fit - it causes rendering issues when scrolling
          // The terminal will use the cols/rows we specified
          console.log('[Terminal] Skipping initial fit to avoid viewport issues')

          // Re-fit on window resize only
          const handleResize = () => {
            try {
              fitAddon.fit()
            } catch (e) {
              // Ignore
            }
          }
          window.addEventListener('resize', handleResize)

          xtermRef.current = terminal
          fitAddonRef.current = fitAddon
          console.log('[Terminal Init] xterm.js terminal initialized successfully')
          console.log('[Terminal Init] Terminal element:', terminalRef.current)
          console.log('[Terminal Init] Terminal dimensions:', terminalRef.current?.offsetWidth, 'x', terminalRef.current?.offsetHeight)
        }
      } catch (err) {
        console.error('Failed to initialize terminal:', err)
        setError(`Failed to initialize terminal: ${err}`)
      }
    }, 100)

    return () => {
      clearTimeout(timeoutId)
      window.removeEventListener('resize', () => {})
      if (xtermRef.current) {
        xtermRef.current.dispose()
        xtermRef.current = null
      }
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

      try {
        renderFrame()
      } catch (err) {
        console.error('[renderLoop] Frame render error:', err)
        setError(`Render error: ${err}`)
        stopWebcam()
        return
      }

      // Update FPS counter every second
      frameCountRef.current++
      if (now - fpsUpdateTimeRef.current >= 1000) {
        const currentFps = Math.round(frameCountRef.current / ((now - fpsUpdateTimeRef.current) / 1000))
        setFps(currentFps.toString())
        console.log('[FPS]', currentFps, 'frames in last second')
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

    if (frameCountRef.current % 60 === 0) {
      console.log('[renderFrame] Called, frame:', frameCountRef.current, 'terminal:', !!terminal, 'wasm:', isWasmReady())
    }

    if (!video || !canvas || !terminal || !isWasmReady()) return

    const ctx = canvas.getContext('2d', { willReadFrequently: true })
    if (!ctx) {
      if (frameCountRef.current % 60 === 0) {
        console.log('[renderFrame] No context!')
      }
      return
    }

    // Draw video frame to canvas
    ctx.drawImage(video, 0, 0, canvas.width, canvas.height)

    // Get RGBA pixel data
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height)
    const rgbaData = new Uint8Array(imageData.data)

    if (frameCountRef.current % 60 === 0) {
      console.log('[renderFrame] Got imageData, size:', rgbaData.length)
    }

    // Convert to ASCII using WASM
    if (frameCountRef.current % 60 === 0) {
      console.log('[renderFrame] Calling convertFrameToAscii...')
    }

    const asciiArt = convertFrameToAscii(
      rgbaData,
      canvas.width,
      canvas.height,
      ASCII_WIDTH,
      ASCII_HEIGHT
    )

    if (frameCountRef.current % 60 === 0) {
      console.log('[renderFrame] Got asciiArt, length:', asciiArt?.length)
    }

    // Efficient rendering: move cursor to home and overwrite in one operation
    // \x1b[H moves cursor to home (top-left)
    // Format as lines with \r\n line endings
    const lines: string[] = []
    for (let i = 0; i < ASCII_HEIGHT; i++) {
      lines.push(asciiArt.substring(i * ASCII_WIDTH, (i + 1) * ASCII_WIDTH))
    }

    const output = '\x1b[H' + lines.join('\r\n')
    if (frameCountRef.current % 60 === 0) {
      console.log('[terminal.write] Writing frame:', frameCountRef.current, 'output length:', output.length)
      console.log('[terminal.write] Terminal element in DOM:', document.contains(terminalRef.current))
      console.log('[terminal.write] Terminal visible:', terminalRef.current?.offsetHeight, 'x', terminalRef.current?.offsetWidth)
    }
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
      {/* Hidden video and canvas for capture - invisible but rendered */}
      <div style={{ opacity: 0, position: 'absolute', pointerEvents: 'none', width: 0, height: 0, overflow: 'hidden' }}>
        <video ref={videoRef} autoPlay muted playsInline style={{ width: '640px', height: '480px' }} />
        <canvas ref={canvasRef} />
      </div>

      {/* Controls and info */}
      <div className="px-4 py-3 flex-shrink-0 border-b border-terminal-8">
        <div className="flex items-center justify-between mb-2">
          <h2 className="text-sm font-semibold">ASCII Mirror ({ASCII_WIDTH}x{ASCII_HEIGHT})</h2>
          <div className="text-xs text-terminal-8">
            FPS: <span className="text-terminal-2">{fps}</span>
          </div>
        </div>
        <div className="flex gap-2">
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
        </div>
      </div>

      {/* ASCII output */}
      <div className="flex-1 px-4 py-2 flex flex-col">
        <div
          ref={terminalRef}
          className="flex-1 rounded bg-terminal-bg"
          style={{ overflow: 'hidden', pointerEvents: 'none', minHeight: '400px' }}
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
