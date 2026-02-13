import { useRef, useCallback } from 'react'

/**
 * Hook for capturing video frames from a canvas
 * Handles common canvas setup and frame capture logic
 */
export function useCanvasCapture(
  videoRef: React.RefObject<HTMLVideoElement | null>,
  canvasRef: React.RefObject<HTMLCanvasElement | null>
) {
  const capturedDataRef = useRef<{ data: Uint8Array; width: number; height: number } | null>(null)

  const captureFrame = useCallback((): { data: Uint8Array; width: number; height: number } | null => {
    const video = videoRef.current
    const canvas = canvasRef.current

    if (!video || !canvas) {
      console.warn('[useCanvasCapture] Video or canvas not ready')
      return null
    }

    if (canvas.width === 0 || canvas.height === 0) {
      console.warn('[useCanvasCapture] Canvas dimensions not set:', canvas.width, canvas.height)
      return null
    }

    const ctx = canvas.getContext('2d', { willReadFrequently: true })
    if (!ctx) {
      console.error('[useCanvasCapture] Failed to get canvas 2D context')
      return null
    }

    try {
      // Verify video has data
      if (video.videoWidth === 0 || video.videoHeight === 0) {
        console.warn('[useCanvasCapture] Video not ready - no dimensions:', video.videoWidth, video.videoHeight)
        return null
      }

      ctx.drawImage(video, 0, 0, canvas.width, canvas.height)
      const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height)
      const rgbaData = new Uint8Array(imageData.data)

      return {
        data: rgbaData,
        width: canvas.width,
        height: canvas.height
      }
    } catch (err) {
      console.error('[useCanvasCapture] Failed to capture frame:', err)
      if (err instanceof Error && err.message.includes('cross-origin')) {
        console.error('[useCanvasCapture] Cross-origin error - check CORS settings')
      }
      return null
    }
  }, [])

  return { captureFrame, capturedDataRef }
}
