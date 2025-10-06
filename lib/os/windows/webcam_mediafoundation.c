#ifdef _WIN32

#define COBJMACROS
#include "os/webcam.h"
#include "common.h"
#include "platform/windows_compat.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdlib.h>

// Windows Media Foundation webcam implementation

struct webcam_context_t {
  IMFMediaSource *device;
  IMFSourceReader *reader;
  int width;
  int height;
  BOOL mf_initialized;
  BOOL com_initialized;
};

static HRESULT enumerate_devices_and_print(void) {
  IMFAttributes *attr = NULL;
  IMFActivate **devices = NULL;
  UINT32 count = 0;
  HRESULT hr;

  // Create attribute store for device enumeration
  hr = MFCreateAttributes(&attr, 1);
  if (FAILED(hr)) {
    log_error("Failed to create MF attributes: 0x%08x", hr);
    return hr;
  }

  // Set the device type to video capture
  hr =
      IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    log_error("Failed to set MF device type: 0x%08x", hr);
    goto cleanup;
  }

  // Enumerate video capture devices
  hr = MFEnumDeviceSources(attr, &devices, &count);
  if (FAILED(hr)) {
    log_error("Failed to enumerate MF devices: 0x%08x", hr);
    goto cleanup;
  }

  log_info("Found %d video capture device(s):", count);
  for (UINT32 i = 0; i < count; i++) {
    LPWSTR friendlyName = NULL;
    UINT32 nameLength = 0;

    hr = IMFActivate_GetAllocatedString(devices[i], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &nameLength);
    if (SUCCEEDED(hr) && friendlyName) {
      // Convert wide string to multibyte for logging
      int len = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, NULL, 0, NULL, NULL);
      if (len > 0) {
        char *mbName = malloc(len);
        if (mbName && WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, mbName, len, NULL, NULL)) {
          log_info("  Device %d: %s", i, mbName);
        }
        free(mbName);
      }
      CoTaskMemFree(friendlyName);
    } else {
      log_info("  Device %d: <Unknown Name>", i);
    }
  }

cleanup:
  if (devices) {
    for (UINT32 i = 0; i < count; i++) {
      if (devices[i]) {
        IMFActivate_Release(devices[i]);
      }
    }
    CoTaskMemFree(devices);
  }
  if (attr) {
    IMFAttributes_Release(attr);
  }

  return hr;
}

int webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  log_info("Opening Windows webcam with Media Foundation, device index %d", device_index);

  webcam_context_t *cam;
  SAFE_MALLOC(cam, sizeof(webcam_context_t), webcam_context_t *);

  // Initialize all fields
  cam->device = NULL;
  cam->reader = NULL;
  cam->width = 0;
  cam->height = 0;
  cam->mf_initialized = FALSE;
  cam->com_initialized = FALSE;

  HRESULT hr;
  IMFAttributes *attr = NULL;
  IMFActivate **devices = NULL;
  UINT32 count = 0;
  int result = -1;

  // Initialize COM
  hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr)) {
    cam->com_initialized = TRUE;
  } else if (hr != RPC_E_CHANGED_MODE) {
    log_error("Failed to initialize COM: 0x%08x", hr);
    goto error;
  }

  // Initialize Media Foundation
  hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
  if (FAILED(hr)) {
    log_error("Failed to startup Media Foundation: 0x%08x", hr);
    goto error;
  }
  cam->mf_initialized = TRUE;

  // Enumerate and print all devices
  enumerate_devices_and_print();

  // Create attribute store for device enumeration
  hr = MFCreateAttributes(&attr, 1);
  if (FAILED(hr)) {
    log_error("Failed to create MF attributes: 0x%08x", hr);
    goto error;
  }

  hr =
      IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    log_error("Failed to set MF device type: 0x%08x", hr);
    goto error;
  }

  // Enumerate devices
  hr = MFEnumDeviceSources(attr, &devices, &count);
  if (FAILED(hr)) {
    log_error("Failed to enumerate MF devices: 0x%08x", hr);
    goto error;
  }

  if (count == 0) {
    log_error("No video capture devices found");
    hr = E_FAIL;
    goto error;
  }

  if (device_index >= count) {
    log_error("Device index %d out of range (0-%d)", device_index, count - 1);
    hr = E_FAIL;
    goto error;
  }

  // Create media source for the specified device
  hr = IMFActivate_ActivateObject(devices[device_index], &IID_IMFMediaSource, (void **)&cam->device);
  log_info("IMFActivate_ActivateObject returned: 0x%08x", hr);

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to activate MF device: 0x%08x", hr);
    log_error("DEBUG: Common error codes:");
    log_error("  0x80070005 = E_ACCESSDENIED (device in use)");
    log_error("  0xc00d3704 = Device already in use");
    log_error("  0xc00d3e85 = MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED");
    result = ASCIICHAT_ERR_WEBCAM_IN_USE;
    goto error;
  }

  // Create source reader
  hr = MFCreateSourceReaderFromMediaSource(cam->device, NULL, &cam->reader);
  log_info("MFCreateSourceReaderFromMediaSource returned: 0x%08x", hr);

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to create MF source reader: 0x%08x", hr);
    result = ASCIICHAT_ERR_WEBCAM_IN_USE;
    goto error;
  }

  // IMPORTANT: Select the video stream first before configuring
  hr = IMFSourceReader_SetStreamSelection(cam->reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
  log_info("SetStreamSelection (deselect all) returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_warn("Failed to deselect all streams: 0x%08x", hr);
  }

  hr = IMFSourceReader_SetStreamSelection(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
  log_info("SetStreamSelection (select video) returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_error("Failed to select video stream: 0x%08x", hr);
    goto error;
  }

  // Try to set a native format first (don't force RGB24 which might not be supported)
  IMFMediaType *nativeType = NULL;
  DWORD mediaTypeIndex = 0;
  BOOL formatSet = FALSE;

  // Try to find a suitable native format
  while (!formatSet) {
    hr = IMFSourceReader_GetNativeMediaType(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, mediaTypeIndex,
                                            &nativeType);
    if (FAILED(hr)) {
      break; // No more media types
    }

    // Check if this is a video format we can use
    GUID subtype;
    hr = IMFMediaType_GetGUID(nativeType, &MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr)) {
      // Accept common formats: YUY2, NV12, RGB24, RGB32, MJPEG
      const char *format_name = "UNKNOWN";
      if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2))
        format_name = "YUY2";
      else if (IsEqualGUID(&subtype, &MFVideoFormat_NV12))
        format_name = "NV12";
      else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB24))
        format_name = "RGB24";
      else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB32))
        format_name = "RGB32";
      else if (IsEqualGUID(&subtype, &MFVideoFormat_MJPG))
        format_name = "MJPEG";

      log_info("Found format %s at index %d, attempting to set", format_name, mediaTypeIndex);

      if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2) || IsEqualGUID(&subtype, &MFVideoFormat_NV12) ||
          IsEqualGUID(&subtype, &MFVideoFormat_RGB24) || IsEqualGUID(&subtype, &MFVideoFormat_RGB32) ||
          IsEqualGUID(&subtype, &MFVideoFormat_MJPG)) {

        hr = IMFSourceReader_SetCurrentMediaType(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, nativeType);
        log_info("SetCurrentMediaType returned: 0x%08x", hr);
        if (SUCCEEDED(hr)) {
          log_info("Set native media type at index %d", mediaTypeIndex);
          formatSet = TRUE;
        }
      }
    }

    IMFMediaType_Release(nativeType);
    mediaTypeIndex++;
  }

  if (!formatSet) {
    log_warn("Could not set a native format, using device default");
  }

  // Flush the reader to clear any buffered data
  hr = IMFSourceReader_Flush(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  log_info("IMFSourceReader_Flush returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_warn("Failed to flush source reader: 0x%08x", hr);
  }

  // Get actual media type and dimensions
  IMFMediaType *currentType = NULL;
  hr = IMFSourceReader_GetCurrentMediaType(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
  if (SUCCEEDED(hr)) {
    UINT64 frameSize = 0;
    hr = IMFMediaType_GetUINT64(currentType, &MF_MT_FRAME_SIZE, &frameSize);
    if (SUCCEEDED(hr)) {
      cam->width = (int)(frameSize >> 32);
      cam->height = (int)(frameSize & 0xFFFFFFFF);
      log_info("Media Foundation webcam opened: %dx%d", cam->width, cam->height);
    } else {
      // Default resolution if we can't get the actual size
      cam->width = 640;
      cam->height = 480;
      log_warn("Could not get frame size, using default 640x480");
    }
    IMFMediaType_Release(currentType);
  } else {
    cam->width = 640;
    cam->height = 480;
    log_warn("Could not get media type, using default 640x480");
  }

  DWORD streamIndex, flags;
  LONGLONG timestamp;
  IMFSample *sample = NULL;

  hr = IMFSourceReader_ReadSample(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  0, // Regular synchronous read
                                  &streamIndex, &flags, &timestamp, &sample);

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to read test frame during initialization: 0x%08x", hr);
    result = ASCIICHAT_ERR_WEBCAM_IN_USE;
    goto error;
  }

  // Clean up the test sample if we got one
  if (sample) {
    IMFSample_Release(sample);
  }

  // Cleanup enumeration resources on success
  if (devices) {
    for (UINT32 i = 0; i < count; i++) {
      if (devices[i]) {
        IMFActivate_Release(devices[i]);
      }
    }
    CoTaskMemFree(devices);
    devices = NULL;
  }
  if (attr) {
    IMFAttributes_Release(attr);
    attr = NULL;
  }

  *ctx = cam;
  return 0;

error:
  // Only cleanup devices and attr if they haven't been cleaned up yet
  if (devices) {
    for (UINT32 i = 0; i < count; i++) {
      if (devices[i]) {
        IMFActivate_Release(devices[i]);
      }
    }
    CoTaskMemFree(devices);
  }
  if (attr) {
    IMFAttributes_Release(attr);
  }
  if (cam->reader) {
    IMFSourceReader_Release(cam->reader);
  }
  if (cam->device) {
    IMFMediaSource_Release(cam->device);
  }
  if (cam->mf_initialized) {
    MFShutdown();
  }
  if (cam->com_initialized) {
    CoUninitialize();
  }
  if (cam) {
    SAFE_FREE(cam);
  }
  return result;
}

void webcam_cleanup_context(webcam_context_t *ctx) {
  if (ctx) {
    if (ctx->reader) {
      IMFSourceReader_Release(ctx->reader);
      ctx->reader = NULL;
    }
    if (ctx->device) {
      IMFMediaSource_Release(ctx->device);
      ctx->device = NULL;
    }
    if (ctx->mf_initialized) {
      MFShutdown();
      ctx->mf_initialized = FALSE;
    }
    if (ctx->com_initialized) {
      CoUninitialize();
      ctx->com_initialized = FALSE;
    }
    SAFE_FREE(ctx);
    log_info("Windows Media Foundation webcam closed");
  }
}

image_t *webcam_read_context(webcam_context_t *ctx) {
  static int call_count = 0;
  call_count++;

  if (!ctx || !ctx->reader) {
    log_error("DEBUG: webcam_read_context call #%d - NULL context or reader (ctx=%p, reader=%p)", call_count, ctx,
              ctx ? ctx->reader : NULL);
    return NULL;
  }

  HRESULT hr;
  IMFSample *sample = NULL;
  DWORD streamIndex, flags;
  LONGLONG timestamp;

  // Read a sample from the source reader
  // Use 0 for synchronous blocking read (DRAIN flag was wrong - that's for EOF)
  hr = IMFSourceReader_ReadSample(ctx->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  0, // Regular synchronous read
                                  &streamIndex, &flags, &timestamp, &sample);

  // Check for stream tick or other non-data flags
  if (SUCCEEDED(hr) && (flags & MF_SOURCE_READERF_STREAMTICK)) {
    log_info("Received stream tick, no sample yet");
    return NULL;
  }

  if (SUCCEEDED(hr) && (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
    log_warn("End of stream reached");
    return NULL;
  }

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to read MF sample on FIRST attempt: 0x%08x", hr);
    log_error("DEBUG: Error code details:");
    log_error("  0x80070005 = E_ACCESSDENIED (device in use)");
    log_error("  0xc00d3704 = Device already in use");
    log_error("  0xc00d3e85 = MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED");
    log_error("  0x80004005 = E_FAIL (generic failure)");
    log_error("  0xc00d36b2 = MF_E_INVALIDREQUEST");
    log_error("  0xc00d36c4 = MF_E_HW_MFT_FAILED_START_STREAMING");

    // Exit immediately on FIRST error - device is likely in use
    exit(ASCIICHAT_ERR_WEBCAM_IN_USE);
  }

  if (!sample) {
    static int null_count = 0;
    null_count++;
    if (null_count <= 10) {
      // This is normal when using DRAIN flag - it means no sample ready yet
      if (null_count == 1) {
        log_info("No sample available yet (this is normal during startup)");
      }
    } else if (null_count > 50) {
      // Too many consecutive NULL samples - likely exclusive access issue
      exit(ASCIICHAT_ERR_WEBCAM_IN_USE);
    }
    return NULL;
  }

  // Get the media buffer from the sample
  IMFMediaBuffer *buffer = NULL;
  hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
  if (FAILED(hr)) {
    static int buffer_fail_count = 0;
    buffer_fail_count++;
    log_debug("Failed to get contiguous buffer: 0x%08x (failure #%d)", hr, buffer_fail_count);

    if (buffer_fail_count > 20) {
      log_error("CRITICAL: Failed to get media buffer %d times - webcam likely in use", buffer_fail_count);
      log_error("EXITING WITH ERROR CODE: %d", ASCIICHAT_ERR_WEBCAM_IN_USE);
      exit(ASCIICHAT_ERR_WEBCAM_IN_USE);
    }

    IMFSample_Release(sample);
    return NULL;
  }

  // Lock the buffer and get the data
  BYTE *bufferData = NULL;
  DWORD bufferLength = 0;
  hr = IMFMediaBuffer_Lock(buffer, &bufferData, NULL, &bufferLength);
  if (FAILED(hr)) {
    static int lock_fail_count = 0;
    lock_fail_count++;
    log_debug("Failed to lock MF buffer: 0x%08x (failure #%d)", hr, lock_fail_count);

    if (lock_fail_count > 20) {
      log_error("CRITICAL: Failed to lock media buffer %d times - webcam likely in use", lock_fail_count);
      log_error("EXITING WITH ERROR CODE: %d", ASCIICHAT_ERR_WEBCAM_IN_USE);
      exit(ASCIICHAT_ERR_WEBCAM_IN_USE);
    }

    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  // Query the ACTUAL format from Media Foundation - NO GUESSING!
  IMFMediaType *currentType = NULL;
  HRESULT hr2 =
      IMFSourceReader_GetCurrentMediaType(ctx->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);

  if (FAILED(hr2)) {
    log_error("Failed to get current media type: 0x%08x", hr2);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  // Get the actual format GUID
  GUID subtype = {0};
  HRESULT hrGuid = IMFMediaType_GetGUID(currentType, &MF_MT_SUBTYPE, &subtype);
  if (FAILED(hrGuid)) {
    log_error("Failed to get format subtype: 0x%08x", hrGuid);
    IMFMediaType_Release(currentType);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  // Get actual dimensions (packed as UINT64)
  UINT64 frameSize = 0;
  HRESULT hrSize = IMFMediaType_GetUINT64(currentType, &MF_MT_FRAME_SIZE, &frameSize);
  if (FAILED(hrSize)) {
    log_error("Failed to get frame size: 0x%08x", hrSize);
    IMFMediaType_Release(currentType);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  UINT32 actualWidth = (UINT32)(frameSize >> 32);
  UINT32 actualHeight = (UINT32)(frameSize & 0xFFFFFFFF);

  // Get the stride (bytes per row)
  UINT32 stride = 0;
  HRESULT hrStride = IMFMediaType_GetUINT32(currentType, &MF_MT_DEFAULT_STRIDE, &stride);
  if (FAILED(hrStride)) {
    // Calculate stride based on format
    if (IsEqualGUID(&subtype, &MFVideoFormat_NV12)) {
      stride = actualWidth; // NV12 Y plane stride
    } else if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2)) {
      stride = actualWidth * 2; // YUY2 stride
    } else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB24)) {
      stride = actualWidth * 3; // RGB24 stride
    } else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB32)) {
      stride = actualWidth * 4; // RGB32 stride
    } else {
      log_error("Unknown format, cannot calculate stride");
      IMFMediaType_Release(currentType);
      IMFMediaBuffer_Unlock(buffer);
      IMFMediaBuffer_Release(buffer);
      IMFSample_Release(sample);
      return NULL;
    }
  }

  // Log the actual format we're dealing with
  const char *formatName = "UNKNOWN";
  if (IsEqualGUID(&subtype, &MFVideoFormat_NV12))
    formatName = "NV12";
  else if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2))
    formatName = "YUY2";
  else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB24))
    formatName = "RGB24";
  else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB32))
    formatName = "RGB32";
  else if (IsEqualGUID(&subtype, &MFVideoFormat_MJPG))
    formatName = "MJPEG";

  IMFMediaType_Release(currentType);

  // Create image_t structure with ACTUAL dimensions
  image_t *img;
  SAFE_MALLOC(img, sizeof(image_t), image_t *);

  img->w = actualWidth;
  img->h = actualHeight;
  SAFE_MALLOC(img->pixels, actualWidth * actualHeight * sizeof(rgb_t), rgb_t *);

  // Convert based on the ACTUAL format from Media Foundation
  if (IsEqualGUID(&subtype, &MFVideoFormat_RGB24)) {
    // RGB24 format (BGR order in Media Foundation)
    for (UINT32 i = 0; i < actualWidth * actualHeight; i++) {
      img->pixels[i].b = bufferData[i * 3 + 0]; // B
      img->pixels[i].g = bufferData[i * 3 + 1]; // G
      img->pixels[i].r = bufferData[i * 3 + 2]; // R
    }
  } else if (IsEqualGUID(&subtype, &MFVideoFormat_RGB32)) {
    // RGB32 format (BGRX order in Media Foundation)
    for (UINT32 i = 0; i < actualWidth * actualHeight; i++) {
      img->pixels[i].b = bufferData[i * 4 + 0]; // B
      img->pixels[i].g = bufferData[i * 4 + 1]; // G
      img->pixels[i].r = bufferData[i * 4 + 2]; // R
      // Skip alpha channel at [i * 4 + 3]
    }
  } else if (IsEqualGUID(&subtype, &MFVideoFormat_NV12)) {
    // NV12 format: Y plane followed by interleaved UV plane
    // Y plane: one byte per pixel
    // UV plane: two bytes (U,V) for each 2x2 pixel block

    BYTE *yPlane = bufferData;
    BYTE *uvPlane = bufferData + (stride * actualHeight); // UV plane starts after Y plane

    for (UINT32 y = 0; y < actualHeight; y++) {
      for (UINT32 x = 0; x < actualWidth; x++) {
        // Get Y value for this pixel
        int yValue = yPlane[y * stride + x];

        // Get UV values (shared by 2x2 pixel blocks)
        UINT32 uvRow = y / 2;
        UINT32 uvCol = x / 2;
        UINT32 uvIndex = uvRow * stride + uvCol * 2; // UV pairs are interleaved

        int uValue = uvPlane[uvIndex];
        int vValue = uvPlane[uvIndex + 1];

        // Convert YUV to RGB using ITU-R BT.601 coefficients
        int C = yValue - 16;
        int D = uValue - 128;
        int E = vValue - 128;

        int R = (298 * C + 409 * E + 128) >> 8;
        int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
        int B = (298 * C + 516 * D + 128) >> 8;

        // Clamp and store
        UINT32 pixelIndex = y * actualWidth + x;
        img->pixels[pixelIndex].r = clamp_rgb(R);
        img->pixels[pixelIndex].g = clamp_rgb(G);
        img->pixels[pixelIndex].b = clamp_rgb(B);
      }
    }
  } else if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2)) {
    // YUY2 format: packed Y0 U Y1 V (4 bytes = 2 pixels)
    // Use the stride from Media Foundation for proper row alignment

    for (UINT32 y = 0; y < actualHeight; y++) {
      for (UINT32 x = 0; x < actualWidth; x += 2) {
        // Calculate source index for this pixel pair
        // Use the ACTUAL stride from Media Foundation (includes padding)
        int src_idx = y * stride + x * 2;

        // Bounds check to prevent buffer overrun
        if (src_idx + 3 >= (int)bufferLength) {
          break;
        }

        // Extract YUY2 components for this pixel pair
        // YUY2 format: Y0 U Y1 V (2 pixels in 4 bytes)
        int y0 = bufferData[src_idx];     // First pixel Y (luma)
        int u = bufferData[src_idx + 1];  // Shared U (Cb) - blue difference
        int y1 = bufferData[src_idx + 2]; // Second pixel Y (luma)
        int v = bufferData[src_idx + 3];  // Shared V (Cr) - red difference

        // Convert YUV to RGB using ITU-R BT.601 coefficients
        int C = y0 - 16;
        int D = u - 128;
        int E = v - 128;

        // First pixel
        int R = (298 * C + 409 * E + 128) >> 8;
        int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
        int B = (298 * C + 516 * D + 128) >> 8;

        // Store first pixel
        UINT32 pixelIndex = y * actualWidth + x;
        img->pixels[pixelIndex].r = clamp_rgb(R);
        img->pixels[pixelIndex].g = clamp_rgb(G);
        img->pixels[pixelIndex].b = clamp_rgb(B);

        // Second pixel (if within bounds)
        if (x + 1 < actualWidth) {
          C = y1 - 16;
          R = (298 * C + 409 * E + 128) >> 8;
          G = (298 * C - 100 * D - 208 * E + 128) >> 8;
          B = (298 * C + 516 * D + 128) >> 8;

          pixelIndex = y * actualWidth + (x + 1);
          img->pixels[pixelIndex].r = clamp_rgb(R);
          img->pixels[pixelIndex].g = clamp_rgb(G);
          img->pixels[pixelIndex].b = clamp_rgb(B);
        }
      }
    }
  } else {
    // Unsupported format
    log_error("Unsupported format from Media Foundation: %s", formatName);
    image_destroy(img);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  // Unlock and cleanup
  IMFMediaBuffer_Unlock(buffer);
  IMFMediaBuffer_Release(buffer);
  IMFSample_Release(sample);

  return img;
}

int webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  if (!ctx || !width || !height)
    return -1;

  *width = ctx->width;
  *height = ctx->height;
  return 0;
}

#endif
