import { getMirrorModule } from "@ascii-chat/shared/wasm";

interface CapturedFrame {
  data: Uint8Array;
  width: number;
  height: number;
}

/** C owns both the orientation decision and pixel transform for every upload codec. */
export function orientUpload(
  canvas: HTMLCanvasElement,
  frame: CapturedFrame,
): CapturedFrame {
  const module = getMirrorModule();
  if (!module)
    throw new Error("Initialize C rendering before uploading frames");
  if (!module._wasm_capture_transform_required()) return frame;
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (!context) throw new Error("Capture canvas is unavailable");
  const image = context.getImageData(0, 0, frame.width, frame.height);
  const pointer = module._malloc(image.data.length);
  if (!pointer) throw new Error("Cannot allocate capture orientation buffer");
  try {
    module.HEAPU8.set(image.data, pointer);
    if (
      module._wasm_capture_transform_rgba(
        pointer,
        frame.width,
        frame.height,
      ) !== 0
    )
      throw new Error("Invalid capture dimensions");
    image.data.set(
      module.HEAPU8.subarray(pointer, pointer + image.data.length),
    );
    context.putImageData(image, 0, 0);
    // Preserve the original frame for local C conversion; only the upload sees
    // this transformed copy, so local rendering cannot apply the choice twice.
    return {
      ...frame,
      data: frame.data.length ? new Uint8Array(image.data) : frame.data,
    };
  } finally {
    module._free(pointer);
  }
}
