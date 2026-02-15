import { useEffect, useLayoutEffect, useRef, useState, useCallback } from 'react'
import {
  cleanupClientWasm,
  ConnectionState,
  PacketType,
} from '../wasm/client'
import { ClientConnection } from '../network/ClientConnection'
import { parseAsciiFrame } from '../network/AsciiFrameParser'
import { AsciiRenderer, AsciiRendererHandle } from '../components/AsciiRenderer'
import { ConnectionPanelModal } from '../components/ConnectionPanelModal'
import { Settings, SettingsConfig } from '../components/Settings'
import { WebClientHead } from '../components/WebClientHead'
import { useCanvasCapture } from '../hooks/useCanvasCapture'

const CAPABILITIES_PACKET_SIZE = 160
const STREAM_TYPE_VIDEO = 0x01
const STREAM_TYPE_AUDIO = 0x02

function buildStreamStartPacket(includeAudio: boolean = false): Uint8Array {
  const streamType = includeAudio ? (STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO) : STREAM_TYPE_VIDEO
  const buf = new ArrayBuffer(4)
  const view = new DataView(buf)

  // Network byte order (big-endian)
  view.setUint32(0, streamType, false)

  return new Uint8Array(buf)
}

function buildCapabilitiesPacket(cols: number, rows: number): Uint8Array {
  const buf = new ArrayBuffer(CAPABILITIES_PACKET_SIZE)
  const view = new DataView(buf)
  const bytes = new Uint8Array(buf)

  // Network byte order (big-endian) - server uses NET_TO_HOST_U32/U16 to read
  view.setUint32(0, 0x0F, false)        // capabilities (color+utf8+etc)
  view.setUint32(4, 3, false)           // color_level (truecolor)
  view.setUint32(8, 16777216, false)    // color_count
  view.setUint32(12, 0, false)          // render_mode (foreground)
  view.setUint16(16, cols, false)       // width
  view.setUint16(18, rows, false)       // height

  // term_type[32] at offset 20
  const termType = new TextEncoder().encode('xterm-256color')
  bytes.set(termType, 20)

  // colorterm[32] at offset 52
  const colorterm = new TextEncoder().encode('truecolor')
  bytes.set(colorterm, 52)

  bytes[84] = 1                         // detection_reliable
  view.setUint32(85, 1, false)          // utf8_support
  view.setUint32(89, 1, false)          // palette_type (PALETTE_STANDARD=1)
  // palette_custom[64] at offset 93 - zeroed
  bytes[157] = 60                       // desired_fps
  bytes[158] = 0                        // color_filter (none)
  bytes[159] = 1                        // wants_padding

  return bytes
}

/**
 * Build IMAGE_FRAME payload: legacy 8-byte header + RGB24 pixel data.
 * Server expects: [width:4][height:4][rgb_data:w*h*3] (network byte order, big-endian)
 */
function buildImageFramePayload(rgbaData: Uint8Array, width: number, height: number): Uint8Array {
  const pixelCount = width * height
  const rgb24Size = pixelCount * 3
  // image_frame_packet_t structure:
  // width(4) + height(4) + pixel_format(4) + compressed_size(4) + checksum(4) + timestamp(4) + rgb24_data
  const headerSize = 24
  const totalSize = headerSize + rgb24Size
  const buf = new ArrayBuffer(totalSize)
  const view = new DataView(buf)
  const bytes = new Uint8Array(buf)

  console.log(`[buildImageFramePayload] Building 24-byte header + ${rgb24Size} bytes RGB24 data = ${totalSize} total`)

  // Fill header (network byte order big-endian)
  view.setUint32(0, width, false)           // width
  view.setUint32(4, height, false)          // height
  view.setUint32(8, 3, false)               // pixel_format: 3 = RGB24
  view.setUint32(12, 0, false)              // compressed_size: 0 (not compressed)
  view.setUint32(16, 0, false)              // checksum: 0 (TODO: calculate proper CRC32 if needed)
  view.setUint32(20, Date.now(), false)     // timestamp: current time in milliseconds

  console.log(`[buildImageFramePayload] Header: width=${width} height=${height} pixel_format=3 compressed_size=0 timestamp=${Date.now()}`)

  // Convert RGBA to RGB24 (strip alpha channel)
  let srcIdx = 0
  let dstIdx = headerSize
  for (let i = 0; i < pixelCount; i++) {
    bytes[dstIdx]     = rgbaData[srcIdx]     // R
    bytes[dstIdx + 1] = rgbaData[srcIdx + 1] // G
    bytes[dstIdx + 2] = rgbaData[srcIdx + 2] // B
    srcIdx += 4
    dstIdx += 3
  }

  console.log(`[buildImageFramePayload] First RGB bytes: ${bytes[24]},${bytes[25]},${bytes[26]} ${bytes[27]},${bytes[28]},${bytes[29]} ${bytes[30]},${bytes[31]},${bytes[32]}`)

  return bytes
}

export function ClientPage() {
  const rendererRef = useRef<AsciiRendererHandle>(null)
  const clientRef = useRef<ClientConnection | null>(null)
  const videoRef = useRef<HTMLVideoElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const streamRef = useRef<MediaStream | null>(null)
  const animationFrameRef = useRef<number | null>(null)
  const lastFrameTimeRef = useRef<number>(0)
  const frameIntervalRef = useRef<number>(1000 / 30) // 30 FPS for sending frames
  const frameCountRef = useRef<number>(0)
  const lastLogTimeRef = useRef<number>(0)
  const receivedFrameCountRef = useRef<number>(0)
  const frameReceiptTimesRef = useRef<number[]>([])

  const [status, setStatus] = useState<string>('Connecting...')
  const [publicKey, setPublicKey] = useState<string>('')
  const [connectionState, setConnectionState] = useState<ConnectionState>(
    ConnectionState.DISCONNECTED
  )
  const [serverUrl, setServerUrl] = useState<string>('ws://localhost:27226')
  const [showModal, setShowModal] = useState(false)
  const [showSettings, setShowSettings] = useState(false)
  const [error, setError] = useState<string>('')
  const [terminalDimensions, setTerminalDimensions] = useState({ cols: 0, rows: 0 })
  const [isWebcamRunning, setIsWebcamRunning] = useState(false)
  const [hasAutoConnected, setHasAutoConnected] = useState(false)

  // Read server URL from query parameter (for E2E tests)
  // Use useLayoutEffect to ensure this runs before render and auto-connect
  useLayoutEffect(() => {
    const params = new URLSearchParams(window.location.search)
    const testServerUrl = params.get('testServerUrl')
    console.log(`[Client] Query params: search="${window.location.search}"`)
    console.log(`[Client] testServerUrl from query: "${testServerUrl}"`)
    if (testServerUrl) {
      console.log(`[Client] Setting serverUrl to: "${testServerUrl}"`)
      setServerUrl(testServerUrl)
    } else {
      console.log(`[Client] No testServerUrl in query, using default: "${serverUrl}"`)
    }
  }, [])

  // Settings state
  const [settings, setSettings] = useState<SettingsConfig>({
    resolution: '640x480',
    targetFps: 30,
    colorMode: 'truecolor',
    colorFilter: 'none',
    palette: 'standard',
    paletteChars: ' =#░░▒▒▓▓██',
    matrixRain: false,
    webcamFlip: false,
  })

  // Use shared canvas capture hook
  const { captureFrame } = useCanvasCapture(videoRef, canvasRef)

  const STATE_NAMES: Record<number, string> = {
    [ConnectionState.DISCONNECTED]: 'Disconnected',
    [ConnectionState.CONNECTING]: 'Connecting',
    [ConnectionState.HANDSHAKE]: 'Performing handshake',
    [ConnectionState.CONNECTED]: 'Connected',
    [ConnectionState.ERROR]: 'Error',
  }

  const handleDimensionsChange = useCallback((dims: { cols: number; rows: number }) => {
    console.log(`[Client] Dimensions changed: ${dims.cols}x${dims.rows}`)
    setTerminalDimensions(dims)

    // If connected, send updated dimensions to server
    if (clientRef.current) {
      console.log(`[Client] ClientRef available, connectionState=${connectionState}`)
      if (connectionState === ConnectionState.CONNECTED) {
        try {
          console.log(`[Client] Sending updated capabilities: ${dims.cols}x${dims.rows}`)
          const payload = buildCapabilitiesPacket(dims.cols, dims.rows)
          clientRef.current.sendPacket(PacketType.CLIENT_CAPABILITIES, payload)
          console.log('[Client] Updated capabilities sent')
        } catch (err) {
          console.error('[Client] Failed to send capabilities on resize:', err)
        }
      } else {
        console.log(`[Client] Not connected (state=${connectionState}), not sending capabilities`)
      }
    } else {
      console.log('[Client] ClientRef not available, cannot send capabilities')
    }
  }, [connectionState])

  const connectToServer = async () => {
    try {
      console.log('[Client] connectToServer() called')
      console.log(`[Client] Server URL: ${serverUrl}`)
      console.log(`[Client] Terminal dimensions state: ${terminalDimensions.cols}x${terminalDimensions.rows}`)
      console.log(`[Client] Starting connection attempt to: ${serverUrl}`)

      setStatus('Connecting...')
      setError('')

      // Disconnect existing connection if any
      if (clientRef.current) {
        console.log('[Client] Disconnecting previous connection')
        clientRef.current.disconnect()
        clientRef.current = null
      }

      const width = terminalDimensions.cols || 80
      const height = terminalDimensions.rows || 40
      console.log(`[Client] Creating ClientConnection with dimensions: ${width}x${height}`)

      const conn = new ClientConnection({
        serverUrl,
        width,
        height,
      })

      conn.onStateChange((state) => {
        const stateName = STATE_NAMES[state] || 'Unknown'
        console.log(`[Client] State change: ${state} (${stateName})`)

        setConnectionState(state)
        setStatus(stateName)

        if (state === ConnectionState.CONNECTED) {
          console.log('[Client] CONNECTED state reached, attempting to send CLIENT_CAPABILITIES and STREAM_START')

          // Check renderer dimensions
          const rendererDims = rendererRef.current?.getDimensions()
          console.log(`[Client] Renderer dimensions: ${rendererDims ? `${rendererDims.cols}x${rendererDims.rows}` : 'null/undefined'}`)
          console.log(`[Client] Renderer ref available: ${rendererRef.current ? 'yes' : 'no'}`)

          // Send terminal capabilities after handshake
          // Use renderer dimensions if available, otherwise use defaults
          const dims = (rendererDims && rendererDims.cols > 0) ? rendererDims : { cols: 80, rows: 40 }
          console.log(`[Client] Using dimensions for capabilities: ${dims.cols}x${dims.rows}`)

          // Send capabilities and stream start with retry logic since WASM state might not match React state immediately
          const sendSetupPackets = () => {
            try {
              console.log(`[Client] Building CLIENT_CAPABILITIES packet (type=${PacketType.CLIENT_CAPABILITIES})`)
              const capsPayload = buildCapabilitiesPacket(dims.cols, dims.rows)
              console.log(`[Client] Payload size: ${capsPayload.length} bytes`)
              console.log(`[Client] Sending CLIENT_CAPABILITIES packet...`)
              conn.sendPacket(PacketType.CLIENT_CAPABILITIES, capsPayload)
              console.log(`[Client] CLIENT_CAPABILITIES sent successfully`)

              // Send STREAM_START to tell server we're about to send video
              console.log(`[Client] Building STREAM_START packet (type=${PacketType.STREAM_START})`)
              const streamPayload = buildStreamStartPacket(false) // Video only, no audio
              console.log(`[Client] Sending STREAM_START packet...`)
              conn.sendPacket(PacketType.STREAM_START, streamPayload)
              console.log(`[Client] STREAM_START sent successfully`)
            } catch (err) {
              console.error('[Client] Failed to send setup packets:', err)
              // Retry after 100ms if it failed
              setTimeout(sendSetupPackets, 100)
            }
          }
          sendSetupPackets()
        }

        if (state === ConnectionState.ERROR) {
          console.error('[Client] Connection error state reached')
          setError('Connection error')
          setShowModal(true)
        }
      })

      conn.onPacketReceived((parsed, decryptedPayload) => {
        const now = performance.now()
        console.log(`[Client] Packet received: type=${parsed.type}, size=${decryptedPayload.length}`)

        if (parsed.type === PacketType.ASCII_FRAME) {
          receivedFrameCountRef.current++
          frameReceiptTimesRef.current.push(now)
          console.log(`[Client] ========== ASCII_FRAME PACKET RECEIVED (COUNT: ${receivedFrameCountRef.current}) ==========`)
          console.log(`[Client] Payload size: ${decryptedPayload.length} bytes`)

          // Log frame receipt timing
          if (frameReceiptTimesRef.current.length > 1) {
            const prevTime = frameReceiptTimesRef.current[frameReceiptTimesRef.current.length - 2]
            const deltaMs = now - prevTime
            console.log(`[Client] Time since last frame: ${deltaMs.toFixed(1)}ms`)
          }
          console.log(`[Client] Total frames received so far: ${receivedFrameCountRef.current}`)

          // Log first 100 bytes as hex for debugging
          const hexPreview = Array.from(decryptedPayload.slice(0, 100))
            .map(b => b.toString(16).padStart(2, '0'))
            .join(' ')
          console.log(`[Client] Hex preview (first 100 bytes): ${hexPreview}`)

          try {
            console.log('[Client] Parsing ASCII frame...')
            const frame = parseAsciiFrame(decryptedPayload)
            console.log(`[Client] Frame header: ${frame.header.width}x${frame.header.height}`)
            console.log(`[Client] Frame ANSI string length: ${frame.ansiString.length} characters`)
            console.log(`[Client] Frame flags: 0x${frame.header.flags.toString(16)}`)
            console.log(`[Client] Frame original_size: ${frame.header.originalSize}, compressed_size: ${frame.header.compressedSize}`)

            // Print first 500 chars of the ASCII art
            console.log(`[Client] ========== ASCII FRAME CONTENT (first 500 chars) ==========`)
            const preview = frame.ansiString.substring(0, 500).replace(/\r\n/g, '\\r\\n\n')
            console.log(preview)
            console.log(`[Client] ========== END FRAME PREVIEW ==========`)

            console.log('[Client] Writing frame to renderer...')
            rendererRef.current?.writeFrame(frame.ansiString)
            console.log('[Client] Frame written successfully')
          } catch (err) {
            console.error('[Client] ========== FRAME PARSING ERROR ==========')
            console.error('[Client] Failed to parse ASCII frame:', err)
            console.error('[Client] Error stack:', err instanceof Error ? err.stack : 'unknown')
            console.error('[Client] ========== END ERROR ==========')
          }
        } else {
          console.log(`[Client] Packet type ${parsed.type} (not ASCII_FRAME=${PacketType.ASCII_FRAME})`)
        }
      })

      console.log('[Client] Calling conn.connect()...')
      await conn.connect()
      console.log('[Client] conn.connect() completed successfully')

      clientRef.current = conn
      const pubKey = conn.getPublicKey() || ''
      console.log(`[Client] Public key set: ${pubKey.substring(0, 20)}...`)
      setPublicKey(pubKey)
    } catch (err) {
      const errMsg = `${err}`
      console.error('[Client] Connection failed:', errMsg)
      console.error('[Client] Error object:', err)
      console.error('[Client] Error stack:', err instanceof Error ? err.stack : 'unknown')
      setStatus(`Error: ${errMsg}`)
      setError(errMsg)
      setShowModal(true)
    }
  }

  const handleDisconnect = () => {
    console.log('[Client] handleDisconnect() called')
    stopWebcam()

    if (clientRef.current) {
      console.log('[Client] Disconnecting from server')
      clientRef.current.disconnect()
      clientRef.current = null
    }

    console.log('[Client] Clearing connection state')
    setConnectionState(ConnectionState.DISCONNECTED)
    setStatus('Disconnected')
    setPublicKey('')
  }

  // Webcam capture loop - must avoid stale closures in RAF recursion
  // Instead, captureAndSendFrame is called by reference within the callback
  const webcamCaptureLoopRef = useRef<(() => void) | null>(null)

  // Inner loop function that doesn't have dependencies - this prevents RAF recursion from breaking
  const createWebcamCaptureLoop = useCallback(() => {
    let callCount = 0
    return () => {
      callCount++
      if (callCount % 30 === 0) console.log(`[RAF] Animation frame called ${callCount} times, connectionState=${connectionState}`)

      const now = performance.now()
      const elapsed = now - lastFrameTimeRef.current

      if (elapsed >= frameIntervalRef.current) {
        lastFrameTimeRef.current = now
        // Call captureAndSendFrame through ref to get the latest version
        const conn = clientRef.current
        if (conn && connectionState === ConnectionState.CONNECTED) {
          const frame = captureFrame()
          if (frame) {
            console.log(`[Client] *** CAPTURE FRAME: ${frame.width}x${frame.height}, building payload...`)
            const payload = buildImageFramePayload(frame.data, frame.width, frame.height)
            frameCountRef.current++

            const now = performance.now()
            if (now - lastLogTimeRef.current > 1000) {
              console.log(`[Client] Sent ${frameCountRef.current} IMAGE_FRAME packets (${frame.width}x${frame.height}, ${payload.length} bytes each)`)
              frameCountRef.current = 0
              lastLogTimeRef.current = now
            }

            try {
              conn.sendPacket(PacketType.IMAGE_FRAME, payload)
            } catch (err) {
              console.error('[Client] Failed to send IMAGE_FRAME:', err)
            }
          }
        }
      }

      // Schedule next frame - must use ref to ensure we always schedule the latest version
      if (webcamCaptureLoopRef.current) {
        animationFrameRef.current = requestAnimationFrame(webcamCaptureLoopRef.current)
      }
    }
  }, [captureFrame, connectionState])

  // Update the ref whenever dependencies change (including connectionState)
  useEffect(() => {
    webcamCaptureLoopRef.current = createWebcamCaptureLoop()
  }, [createWebcamCaptureLoop])

  const startWebcam = useCallback(async () => {
    console.log('[Client] startWebcam() called')
    console.log(`[DEBUG] videoRef.current=${!!videoRef.current}, canvasRef.current=${!!canvasRef.current}`)

    if (!videoRef.current || !canvasRef.current) {
      console.error('[Client] Video or canvas element not ready')
      setError('Video or canvas element not ready')
      return
    }

    console.log(`[DEBUG] connectionState=${connectionState} vs CONNECTED=${ConnectionState.CONNECTED}`)
    if (connectionState !== ConnectionState.CONNECTED) {
      console.error(`[Client] Not connected (state=${connectionState}), cannot start webcam`)
      setError('Must be connected to server before starting webcam')
      return
    }

    console.log('[Client] Passed all initial checks')

    try {
      // Send STREAM_START to notify server we're about to send video
      if (clientRef.current) {
        console.log('[Client] Sending STREAM_START before webcam...')
        const streamPayload = buildStreamStartPacket(false)
        clientRef.current.sendPacket(PacketType.STREAM_START, streamPayload)
        console.log('[Client] STREAM_START sent')
      } else {
        console.log('[Client] clientRef.current is null, skipping STREAM_START')
      }

      const [w, h] = settings.resolution.split('x').map(Number)
      console.log(`[Client] Requesting webcam stream: ${w}x${h}`)

      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: w },
          height: { ideal: h },
          facingMode: 'user',
        },
        audio: false,
      })

      console.log('[Client] Webcam stream acquired')
      streamRef.current = stream
      const video = videoRef.current!
      video.srcObject = stream

      // Set up metadata listener BEFORE playing to catch the event
      const metadataPromise = new Promise<void>((resolve) => {
        const handleMetadata = () => {
          const video = videoRef.current!
          const canvas = canvasRef.current!
          console.log(`[Client] Webcam metadata loaded: ${video.videoWidth}x${video.videoHeight}, videoTime=${video.currentTime}, paused=${video.paused}`)
          canvas.width = video.videoWidth
          canvas.height = video.videoHeight
          console.log(`[Client] Canvas resized to: ${canvas.width}x${canvas.height}`)
          video.removeEventListener('loadedmetadata', handleMetadata)
          resolve()
        }
        videoRef.current!.addEventListener('loadedmetadata', handleMetadata)
      })

      // Now play the video (metadata event may already be queued)
      console.log('[Client] Attempting to play video...')
      try {
        await video.play()
        console.log('[Client] Video is playing')
      } catch (playErr) {
        console.error('[Client] Video play failed (may be expected):', playErr)
      }

      // Wait for metadata with a timeout (5 seconds) in case it never fires
      const timeoutPromise = new Promise<void>((resolve) => {
        setTimeout(() => {
          console.warn('[Client] Metadata timeout - setting canvas dimensions from current video properties')
          if (videoRef.current && canvasRef.current) {
            const video = videoRef.current
            const canvas = canvasRef.current
            if (video.videoWidth > 0 && video.videoHeight > 0) {
              canvas.width = video.videoWidth
              canvas.height = video.videoHeight
              console.log(`[Client] Canvas resized from timeout: ${canvas.width}x${canvas.height}`)
            }
          }
          resolve()
        }, 5000)
      })

      await Promise.race([metadataPromise, timeoutPromise])

      console.log(`[startWebcam] About to start capture loop, videoRef=${videoRef.current ? 'OK' : 'NULL'}, canvasRef=${canvasRef.current ? 'OK' : 'NULL'}`)
      if (videoRef.current) {
        console.log(`[startWebcam] Video: playing=${!videoRef.current.paused}, width=${videoRef.current.videoWidth}, height=${videoRef.current.videoHeight}`)
      }

      setIsWebcamRunning(true)
      lastFrameTimeRef.current = performance.now()
      frameIntervalRef.current = 1000 / settings.targetFps
      console.log(`[Client] Starting webcam capture loop at ${settings.targetFps} FPS`)
      // Start the capture loop using the ref (which always has the latest version)
      if (webcamCaptureLoopRef.current) {
        console.log('[Client] Scheduling first RAF call')
        console.log(`[Client] RAF scheduled - connectionState=${connectionState}, webcamCaptureLoopRef exists=${!!webcamCaptureLoopRef.current}`)
        animationFrameRef.current = requestAnimationFrame(webcamCaptureLoopRef.current)
        console.log(`[Client] RAF ID: ${animationFrameRef.current}`)
      } else {
        console.error('[Client] webcamCaptureLoopRef.current is null - capture loop not ready')
      }
    } catch (err) {
      const errMsg = `Failed to start webcam: ${err}`
      console.error('[Client]', errMsg)
      console.error('[Client] Error:', err)
      setError(errMsg)
    }
  }, [connectionState, settings.resolution, settings.targetFps])

  const stopWebcam = useCallback(() => {
    console.log('[Client] stopWebcam() called')

    if (animationFrameRef.current !== null) {
      console.log('[Client] Cancelling animation frame')
      cancelAnimationFrame(animationFrameRef.current)
      animationFrameRef.current = null
    }

    if (streamRef.current) {
      console.log('[Client] Stopping video tracks')
      streamRef.current.getTracks().forEach(track => track.stop())
      streamRef.current = null
    }

    if (videoRef.current) {
      console.log('[Client] Clearing video source')
      videoRef.current.srcObject = null
    }

    console.log('[Client] Webcam stopped')
    setIsWebcamRunning(false)
  }, [])

  // Auto-connect on mount with serverUrl
  // This depends on serverUrl to ensure it uses the correct URL from query params
  // Use a separate effect to trigger connection only after serverUrl is fully set
  useEffect(() => {
    if (hasAutoConnected) return

    const timer = setTimeout(() => {
      console.log('[Client] Auto-connecting with serverUrl:', serverUrl)
      setHasAutoConnected(true)
      connectToServer()
    }, 0)

    return () => clearTimeout(timer)
  }, [serverUrl])

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      console.log('[Client] Component unmounting')
      stopWebcam()
      if (clientRef.current) {
        clientRef.current.disconnect()
        clientRef.current = null
      }
      cleanupClientWasm()
    }
  }, [])

  // Auto-start webcam once connected
  useEffect(() => {
    if (connectionState === ConnectionState.CONNECTED && !isWebcamRunning) {
      console.log('[Client] Connected and ready, auto-starting webcam...')
      startWebcam()
    }
    // Note: startWebcam is NOT in deps array to avoid circular dependency issues
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connectionState, isWebcamRunning])

  const getStatusDotColor = () => {
    switch (connectionState) {
      case ConnectionState.CONNECTED:
        return 'bg-terminal-2'
      case ConnectionState.CONNECTING:
      case ConnectionState.HANDSHAKE:
        return 'bg-terminal-3'
      case ConnectionState.ERROR:
        return 'bg-terminal-1'
      default:
        return 'bg-terminal-8'
    }
  }

  return (
    <>
      <WebClientHead
        title="Client - ascii-chat Web Client"
        description="Connect to an ascii-chat server. Real-time encrypted video chat rendered as ASCII art in your browser."
        url="https://web.ascii-chat.com/client"
      />
      <div className="flex-1 bg-terminal-bg text-terminal-fg flex flex-col min-h-0">
        {/* Hidden video and canvas for webcam capture */}
        <div style={{ position: 'fixed', bottom: 0, right: 0, width: '1px', height: '1px', overflow: 'hidden', pointerEvents: 'none' }}>
          <video ref={videoRef} autoPlay muted playsInline style={{ width: '640px', height: '480px' }} />
          <canvas ref={canvasRef} />
        </div>

        {/* Settings Panel */}
        {showSettings && (
          <Settings
            config={settings}
            onChange={setSettings}
          />
        )}

        {/* Control bar */}
        <div className="px-4 py-3 flex-shrink-0 border-b border-terminal-8">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="flex items-center gap-2">
                <div className={`w-2 h-2 rounded-full ${getStatusDotColor()}`} />
                <span className="text-sm font-semibold">
                  Client
                </span>
              </div>
              {terminalDimensions.cols > 0 && (
                <span className="text-xs text-terminal-8">
                  {terminalDimensions.cols}x{terminalDimensions.rows}
                </span>
              )}
              <span className="status text-xs text-terminal-8">
                {status}
              </span>
            </div>
            <div className="flex gap-2">
              {connectionState === ConnectionState.CONNECTED && (
                !isWebcamRunning ? (
                  <button
                    onClick={startWebcam}
                    className="px-4 py-2 bg-terminal-2 text-terminal-bg rounded hover:bg-terminal-10 text-sm font-medium"
                  >
                    Start Webcam
                  </button>
                ) : (
                  <button
                    onClick={stopWebcam}
                    className="px-4 py-2 bg-terminal-1 text-terminal-bg rounded hover:bg-terminal-9 text-sm font-medium"
                  >
                    Stop Webcam
                  </button>
                )
              )}
              <button
                onClick={() => setShowModal(true)}
                className="px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
              >
                Connection
              </button>
              <button
                onClick={() => setShowSettings(!showSettings)}
                className="px-4 py-2 bg-terminal-8 text-terminal-fg rounded hover:bg-terminal-7 text-sm"
              >
                Settings
              </button>
            </div>
          </div>
        </div>

        {/* ASCII output */}
        <AsciiRenderer
          ref={rendererRef}
          onDimensionsChange={handleDimensionsChange}
          error={error}
          showFps={isWebcamRunning}
        />

        {/* Connection modal */}
        <ConnectionPanelModal
          isOpen={showModal}
          onClose={() => setShowModal(false)}
          connectionState={connectionState}
          status={status}
          publicKey={publicKey}
          serverUrl={serverUrl}
          onServerUrlChange={setServerUrl}
          onConnect={connectToServer}
          onDisconnect={handleDisconnect}
          isConnected={connectionState === ConnectionState.CONNECTED}
        />
      </div>
    </>
  )
}
