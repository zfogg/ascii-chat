#ifdef _WIN32

#define COBJMACROS
#include "os/webcam.h"
#include "common.h"
#include "platform/windows_compat.h"
#include "util/math.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdlib.h>

// Windows Media Foundation webcam implementation
// Note: MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING and
//       MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING are defined in mfreadwrite.h

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
    return SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to create MF attributes: 0x%08x", hr);
  }

  // Set the device type to video capture
  hr =
      IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to set MF device type: 0x%08x", hr);
    goto cleanup;
  }

  // Enumerate video capture devices
  hr = MFEnumDeviceSources(attr, &devices, &count);
  if (FAILED(hr)) {
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to enumerate MF devices: 0x%08x", hr);
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
        char *mbName = SAFE_MALLOC(len, void *);
        if (mbName && WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, mbName, len, NULL, NULL)) {
          log_info("  Device %d: %s", i, mbName);
        }
        SAFE_FREE(mbName);
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

asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int device_index) {
  log_info("Opening Windows webcam with Media Foundation, device index %d", device_index);

  webcam_context_t *cam;
  cam = SAFE_MALLOC(sizeof(webcam_context_t), webcam_context_t *);

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
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to initialize COM: 0x%08x", hr);
    goto error;
  }

  // Initialize Media Foundation
  hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
  if (FAILED(hr)) {
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to startup Media Foundation: 0x%08x", hr);
    goto error;
  }
  cam->mf_initialized = TRUE;

  // Enumerate and print all devices
  enumerate_devices_and_print();

  // Create attribute store for device enumeration
  hr = MFCreateAttributes(&attr, 1);
  if (FAILED(hr)) {
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to create MF attributes: 0x%08x", hr);
    goto error;
  }

  hr =
      IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  if (FAILED(hr)) {
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to set MF device type: 0x%08x", hr);
    goto error;
  }

  // Enumerate devices
  hr = MFEnumDeviceSources(attr, &devices, &count);
  if (FAILED(hr)) {
    SET_ERRNO_SYS(ERROR_WEBCAM, "Failed to enumerate MF devices: 0x%08x", hr);
    goto error;
  }

  if (count == 0) {
    SET_ERRNO(ERROR_WEBCAM, "No video capture devices found");
    hr = E_FAIL;
    goto error;
  }

  if (device_index >= count) {
    SET_ERRNO(ERROR_WEBCAM, "Device index %d out of range (0-%d)", device_index, count - 1);
    hr = E_FAIL;
    goto error;
  }

  // Create media source for the specified device
  hr = IMFActivate_ActivateObject(devices[device_index], &IID_IMFMediaSource, (void **)&cam->device);
  log_info("IMFActivate_ActivateObject returned: 0x%08x", hr);

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to activate MF device: 0x%08x", hr);
    log_error("  0x80070005 = E_ACCESSDENIED (device in use)");
    log_error("  0xc00d3704 = Device already in use");
    log_error("  0xc00d3e85 = MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED");
    result = ERROR_WEBCAM_IN_USE;
    goto error;
  }

  // Create source reader with GPU-accelerated video processing (Windows 8+)
  // This enables automatic YUV->RGB conversion with hardware acceleration
  IMFAttributes *readerAttrs = NULL;
  hr = MFCreateAttributes(&readerAttrs, 1);
  if (FAILED(hr)) {
    log_error("Failed to create reader attributes: 0x%08x", hr);
    result = ERROR_WEBCAM;
    goto error;
  }

  // Enable advanced video processing for GPU-accelerated YUV->RGB conversion (Windows 8+)
  // This is the recommended attribute for webcam capture with format conversion
  hr = IMFAttributes_SetUINT32(readerAttrs, &MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
  if (FAILED(hr)) {
    log_warn("Failed to set advanced video processing attribute: 0x%08x", hr);
  }

  hr = MFCreateSourceReaderFromMediaSource(cam->device, readerAttrs, &cam->reader);
  log_info("MFCreateSourceReaderFromMediaSource returned: 0x%08x, readerAttrs=%p", hr, (void *)readerAttrs);

  if (readerAttrs) {
    IMFAttributes_Release(readerAttrs);
  }

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to create MF source reader: 0x%08x", hr);
    result = ERROR_WEBCAM_IN_USE;
    goto error;
  }

  // IMPORTANT: Select the video stream first before configuring
  hr = IMFSourceReader_SetStreamSelection(cam->reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
  log_info("SetStreamSelection (deselect all) returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_warn("Failed to deselect all streams: 0x%08x", hr);
  }

  hr = IMFSourceReader_SetStreamSelection(cam->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
  log_info("SetStreamSelection (select video) returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_error("Failed to select video stream: 0x%08x", hr);
    goto error;
  }

  // Request RGB32 output format (BGRA) - more widely supported than RGB24
  // Combined with MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, this enables
  // GPU-accelerated YUV->RGB conversion
  IMFMediaType *rgbType = NULL;
  hr = MFCreateMediaType(&rgbType);
  if (SUCCEEDED(hr)) {
    IMFMediaType_SetGUID(rgbType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(rgbType, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);

    // Use partial type - let MF fill in frame size and other details
    hr = IMFSourceReader_SetCurrentMediaType(cam->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, rgbType);
    IMFMediaType_Release(rgbType);

    if (SUCCEEDED(hr)) {
      log_info("Successfully requested RGB32 output format");
    } else {
      log_warn("Could not set RGB32 format: 0x%08x, will use native format", hr);
      // Don't fail - just use whatever format the camera provides
    }
  }

  // Get actual media type and dimensions
  IMFMediaType *currentType = NULL;
  hr = IMFSourceReader_GetCurrentMediaType(cam->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
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

  hr = IMFSourceReader_ReadSample(cam->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  0, // Regular synchronous read
                                  &streamIndex, &flags, &timestamp, &sample);

  if (FAILED(hr)) {
    log_error("CRITICAL: Failed to read test frame during initialization: 0x%08x", hr);
    result = ERROR_WEBCAM_IN_USE;
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
    CoTaskMemFree((void *)devices);
    devices = NULL;
  }
  if (attr) {
    IMFAttributes_Release(attr);
    attr = NULL;
  }

  *ctx = cam;
  return ASCIICHAT_OK;

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
  if (!ctx || !ctx->reader) {
    return NULL;
  }

  HRESULT hr;
  IMFSample *sample = NULL;
  DWORD streamIndex, flags;
  LONGLONG timestamp;

  // Timing diagnostic: measure ReadSample duration
  LARGE_INTEGER freq, start, end;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&start);

  // Read a sample from the source reader
  // Use 0 for synchronous blocking read (DRAIN flag was wrong - that's for EOF)
  hr = IMFSourceReader_ReadSample(ctx->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                  0, // Regular synchronous read
                                  &streamIndex, &flags, &timestamp, &sample);

  QueryPerformanceCounter(&end);
  double elapsed_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
  log_info("ReadSample took %.2f ms (hr=0x%08x, flags=0x%08x, sample=%p)", elapsed_ms, hr, flags, sample);

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
    log_error("  0x80070005 = E_ACCESSDENIED (device in use)");
    log_error("  0xc00d3704 = Device already in use");
    log_error("  0xc00d3e85 = MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED");
    log_error("  0x80004005 = E_FAIL (generic failure)");
    log_error("  0xc00d36b2 = MF_E_INVALIDREQUEST");
    log_error("  0xc00d36c4 = MF_E_HW_MFT_FAILED_START_STREAMING");

    // Return NULL - device is likely in use
    return NULL;
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
      log_error("Too many consecutive NULL samples (%d) - device likely in use", null_count);
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
    }

    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  // Get frame dimensions from current media type
  IMFMediaType *currentType = NULL;
  hr = IMFSourceReader_GetCurrentMediaType(ctx->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &currentType);
  if (FAILED(hr)) {
    log_error("Failed to get current media type: 0x%08x", hr);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  UINT64 frameSize = 0;
  hr = IMFMediaType_GetUINT64(currentType, &MF_MT_FRAME_SIZE, &frameSize);
  IMFMediaType_Release(currentType);

  if (FAILED(hr)) {
    log_error("Failed to get frame size: 0x%08x", hr);
    IMFMediaBuffer_Unlock(buffer);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  UINT32 width = (UINT32)(frameSize >> 32);
  UINT32 height = (UINT32)(frameSize & 0xFFFFFFFF);

  // Create image_t structure
  image_t *img = SAFE_MALLOC(sizeof(image_t), image_t *);
  img->w = width;
  img->h = height;
  img->pixels = SAFE_MALLOC(width * height * sizeof(rgb_t), rgb_t *);

  // Copy RGB32 data (BGRA order in Media Foundation)
  // Media Foundation converts YUV->RGB32 via GPU-accelerated pipeline
  // RGB32 format is 4 bytes per pixel: B, G, R, A (we ignore alpha)
  for (UINT32 i = 0; i < width * height; i++) {
    img->pixels[i].b = bufferData[i * 4 + 0]; // B
    img->pixels[i].g = bufferData[i * 4 + 1]; // G
    img->pixels[i].r = bufferData[i * 4 + 2]; // R
    // bufferData[i * 4 + 3] is alpha, ignored
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
