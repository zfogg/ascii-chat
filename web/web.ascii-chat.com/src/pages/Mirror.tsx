import { useEffect, useRef, useState } from 'react'
import { XTerm } from '@pablo-lion/xterm-react'
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
  const videoRef = useRef<HTMLVideoElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const xtermRef = useRef<any>(null)
  const fitAddonRef = useRef<FitAddon | null>(null)
  const fpsRef = useRef<HTMLDivElement>(null)
  const [isRunning, setIsRunning] = useState(false)
  const [error, setError] = useState<string>('')
  const streamRef = useRef<MediaStream | null>(null)
  const animationFrameRef = useRef<number | null>(null)
  const lastFrameTimeRef = useRef<number>(0)
  const frameCountRef = useRef<number>(0)
  const fpsUpdateTimeRef = useRef<number>(0)
  const setupDoneRef = useRef(false)

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

  // Ref callback to set up terminal when mounted
  const handleXTermRef = (instance: any) => {
    xtermRef.current = instance

    if (!instance || setupDoneRef.current) return

    // Wait for next tick to ensure terminal is fully initialized
    setTimeout(() => {
      if (!instance.terminal) {
        console.error('[Setup] Terminal not available on instance')
        return
      }

      console.log('[Setup] Terminal ready, setting up FitAddon and disabling pause')
      const terminal = instance.terminal

      const fitAddon = new FitAddon()
      terminal.loadAddon(fitAddon)

      try {
        fitAddon.fit()
        console.log('[Terminal] FitAddon applied successfully')
      } catch (e) {
        console.warn('[Terminal] FitAddon.fit() failed:', e)
      }

      fitAddonRef.current = fitAddon

      // FORCE DISABLE IntersectionObserver pause mechanism
      console.log('[Setup] Attempting to access RenderService via _core...')
      const core = (terminal as any)._core
      console.log('[Setup] _core exists:', !!core)

      if (core) {
        console.log('[Setup] _core properties:', Object.keys(core))
        const renderService = core._renderService
        console.log('[Setup] RenderService exists:', !!renderService)

        if (renderService) {
          console.log('[Terminal] Found RenderService, initial _isPaused =', renderService._isPaused)

          // Override the _handleIntersectionChange to prevent pausing
          const originalHandler = renderService._handleIntersectionChange.bind(renderService)
          renderService._handleIntersectionChange = (entry: any) => {
            console.log('[INTERCEPTED] IntersectionObserver fired:', {
              isIntersecting: entry.isIntersecting,
              intersectionRatio: entry.intersectionRatio,
              time: entry.time
            })
            // Call original but then force unpause
            originalHandler(entry)
            renderService._isPaused = false
            console.log('[FORCED] Set _isPaused = false')
          }

          // Also force it to false immediately
          renderService._isPaused = false
          console.log('[Terminal] Forced _isPaused to false')
        } else {
          console.error('[Terminal] RenderService not found in _core!')
        }
      } else {
        console.error('[Terminal] _core not found! Terminal properties:', Object.keys(terminal))
      }

      const handleResize = () => {
        try {
          fitAddon.fit()
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

      // Update FPS counter every second - direct DOM update to avoid re-renders
      frameCountRef.current++
      if (now - fpsUpdateTimeRef.current >= 1000) {
        const currentFps = Math.round(frameCountRef.current / ((now - fpsUpdateTimeRef.current) / 1000))
        // Update DOM directly without triggering React re-render
        if (fpsRef.current) {
          fpsRef.current.textContent = currentFps.toString()
        }
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
    const terminal = xtermRef.current?.terminal

    // Check _isPaused state every 60 frames
    if (frameCountRef.current % 60 === 0) {
      const renderService = (terminal as any)?._core?._renderService
      const isPaused = renderService?._isPaused
      console.log('[renderFrame] Frame:', frameCountRef.current, 'terminal:', !!terminal, 'wasm:', isWasmReady(), '_isPaused:', isPaused)
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

    // Debug: Check if write is being called and log DOM state
    if (frameCountRef.current % 60 === 0) {
      const scrollY = window.scrollY
      const termEl = xtermRef.current?.elementRef?.current
      const termRect = termEl?.getBoundingClientRect()
      const screenEl = termEl?.querySelector('.xterm-screen')
      const rowsEl = termEl?.querySelector('.xterm-rows')

      console.log('[WRITE] Frame:', frameCountRef.current, 'scroll:', scrollY.toFixed(0),
        'termVisible:', termRect ? `top=${termRect.top.toFixed(0)} bot=${termRect.bottom.toFixed(0)}` : 'N/A',
        'hasScreenEl:', !!screenEl, 'hasRowsEl:', !!rowsEl,
        'writing:', output.substring(0, 30))
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

      {/* Controls and info */}
      <div className="px-4 py-3 flex-shrink-0 border-b border-terminal-8">
        <div className="flex items-center justify-between mb-2">
          <h2 className="text-sm font-semibold">ASCII Mirror ({ASCII_WIDTH}x{ASCII_HEIGHT})</h2>
          <div className="text-xs text-terminal-8">
            FPS: <span className="text-terminal-2" ref={fpsRef}>--</span>
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
      <div className="flex-1 px-4 py-2" style={{ pointerEvents: 'none' }}>
        <XTerm
          ref={handleXTermRef}
          options={{
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
          }}
          className="w-full rounded bg-terminal-bg"
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
