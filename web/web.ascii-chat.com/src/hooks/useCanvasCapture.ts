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
      return null
    }

    const ctx = canvas.getContext('2d', { willReadFrequently: true })
    if (!ctx) {
      return null
    }

    try {
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
      return null
    }
  }, [])

  return { captureFrame, capturedDataRef }
}
