#ifdef _WIN32

#define COBJMACROS
#include "os/webcam.h"
#include "common.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <stdlib.h>

// Windows Media Foundation webcam implementation

struct webcam_context_t {
  IMFMediaSource* device;
  IMFSourceReader* reader;
  int width;
  int height;
  BOOL mf_initialized;
  BOOL com_initialized;
};

static HRESULT enumerate_devices_and_print(void) {
  IMFAttributes* attr = NULL;
  IMFActivate** devices = NULL;
  UINT32 count = 0;
  HRESULT hr;

  // Create attribute store for device enumeration
  hr = MFCreateAttributes(&attr, 1);
  if (FAILED(hr)) {
    log_error("Failed to create MF attributes: 0x%08x", hr);
    return hr;
  }

  // Set the device type to video capture
  hr = IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
                            &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
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
    
    hr = IMFActivate_GetAllocatedString(devices[i], &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
                                       &friendlyName, &nameLength);
    if (SUCCEEDED(hr) && friendlyName) {
      // Convert wide string to multibyte for logging
      int len = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, NULL, 0, NULL, NULL);
      if (len > 0) {
        char* mbName = malloc(len);
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

int webcam_platform_init(webcam_context_t **ctx, unsigned short int device_index) {
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
  IMFAttributes* attr = NULL;
  IMFActivate** devices = NULL;
  UINT32 count = 0;

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

  hr = IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, 
                            &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
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
  hr = IMFActivate_ActivateObject(devices[device_index], &IID_IMFMediaSource, (void**)&cam->device);
  if (FAILED(hr)) {
    log_error("Failed to activate MF device: 0x%08x", hr);
    goto error;
  }

  // Create source reader
  hr = MFCreateSourceReaderFromMediaSource(cam->device, NULL, &cam->reader);
  if (FAILED(hr)) {
    log_error("Failed to create MF source reader: 0x%08x", hr);
    goto error;
  }

  // IMPORTANT: Select the video stream first before configuring
  log_info("About to call IMFSourceReader_SetStreamSelection (deselect all)");
  hr = IMFSourceReader_SetStreamSelection(cam->reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);
  log_info("SetStreamSelection (deselect all) returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_warn("Failed to deselect all streams: 0x%08x", hr);
  }
  
  log_info("About to call IMFSourceReader_SetStreamSelection (select video stream)");
  hr = IMFSourceReader_SetStreamSelection(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
  log_info("SetStreamSelection (select video) returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_error("Failed to select video stream: 0x%08x", hr);
    goto error;
  }

  // Try to set a native format first (don't force RGB24 which might not be supported)
  IMFMediaType* nativeType = NULL;
  DWORD mediaTypeIndex = 0;
  BOOL formatSet = FALSE;
  
  // Try to find a suitable native format
  while (!formatSet) {
    hr = IMFSourceReader_GetNativeMediaType(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 
                                           mediaTypeIndex, &nativeType);
    if (FAILED(hr)) {
      break; // No more media types
    }
    
    // Check if this is a video format we can use
    GUID subtype;
    hr = IMFMediaType_GetGUID(nativeType, &MF_MT_SUBTYPE, &subtype);
    if (SUCCEEDED(hr)) {
      // Accept common formats: YUY2, NV12, RGB24, RGB32, MJPEG
      if (IsEqualGUID(&subtype, &MFVideoFormat_YUY2) ||
          IsEqualGUID(&subtype, &MFVideoFormat_NV12) ||
          IsEqualGUID(&subtype, &MFVideoFormat_RGB24) ||
          IsEqualGUID(&subtype, &MFVideoFormat_RGB32) ||
          IsEqualGUID(&subtype, &MFVideoFormat_MJPG)) {
        
        log_info("About to call IMFSourceReader_SetCurrentMediaType at index %d", mediaTypeIndex);
        hr = IMFSourceReader_SetCurrentMediaType(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM, 
                                                NULL, nativeType);
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
  log_info("About to call IMFSourceReader_Flush");
  hr = IMFSourceReader_Flush(cam->reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM);
  log_info("IMFSourceReader_Flush returned: 0x%08x", hr);
  if (FAILED(hr)) {
    log_warn("Failed to flush source reader: 0x%08x", hr);
  }

  // Get actual media type and dimensions
  IMFMediaType* currentType = NULL;
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

  // Cleanup enumeration resources
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

  *ctx = cam;
  return 0;

error:
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
  SAFE_FREE(cam);
  return -1;
}

void webcam_platform_cleanup(webcam_context_t *ctx) {
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

image_t *webcam_platform_read(webcam_context_t *ctx) {
  if (!ctx || !ctx->reader)
    return NULL;

  HRESULT hr;
  IMFSample* sample = NULL;
  DWORD streamIndex, flags;
  LONGLONG timestamp;




  
  // Read a sample from the source reader
  // Use 0 for synchronous blocking read (DRAIN flag was wrong - that's for EOF)
  hr = IMFSourceReader_ReadSample(ctx->reader, 
                                 MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                 0,  // Regular synchronous read
                                 &streamIndex, &flags, &timestamp, &sample);
  


            (long)hr, (unsigned long)flags, sample);
  
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
    static int error_count = 0;
    error_count++;
    if (error_count <= 5) {
      log_warn("Failed to read MF sample: 0x%08x (error #%d)", hr, error_count);
    }
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
    }
    return NULL;
  }

  log_info("Successfully read MF sample - processing frame");

  // Get the media buffer from the sample
  IMFMediaBuffer* buffer = NULL;
  hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
  if (FAILED(hr)) {
    log_debug("Failed to get contiguous buffer: 0x%08x", hr);
    IMFSample_Release(sample);
    return NULL;
  }

  // Lock the buffer and get the data
  BYTE* bufferData = NULL;
  DWORD bufferLength = 0;
  hr = IMFMediaBuffer_Lock(buffer, &bufferData, NULL, &bufferLength);
  if (FAILED(hr)) {
    log_debug("Failed to lock MF buffer: 0x%08x", hr);
    IMFMediaBuffer_Release(buffer);
    IMFSample_Release(sample);
    return NULL;
  }

  // Create image_t structure
  image_t *img;
  SAFE_MALLOC(img, sizeof(image_t), image_t *);
  
  img->w = ctx->width;
  img->h = ctx->height;
  SAFE_MALLOC(img->pixels, ctx->width * ctx->height * sizeof(rgb_t), rgb_t *);

  // Convert buffer data to RGB pixels
  // Assuming the buffer contains RGB24 data (3 bytes per pixel)
  const int expectedSize = ctx->width * ctx->height * 3;
  
  if ((int)bufferLength >= expectedSize) {
    // Direct RGB24 conversion
    for (int i = 0; i < ctx->width * ctx->height; i++) {
      // Media Foundation RGB24 is typically BGR, so swap R and B
      img->pixels[i].b = bufferData[i * 3 + 0];  // B
      img->pixels[i].g = bufferData[i * 3 + 1];  // G
      img->pixels[i].r = bufferData[i * 3 + 2];  // R
    }
  } else {
    // Buffer size mismatch - fill with a pattern so we can see something
    log_warn("Buffer size mismatch: got %d bytes, expected %d", bufferLength, expectedSize);
    for (int i = 0; i < ctx->width * ctx->height; i++) {
      img->pixels[i].r = (i % 256);     // Red gradient
      img->pixels[i].g = 128;           // Fixed green
      img->pixels[i].b = 255 - (i % 256); // Blue inverse gradient
    }
  }

  // Unlock and cleanup
  IMFMediaBuffer_Unlock(buffer);
  IMFMediaBuffer_Release(buffer);
  IMFSample_Release(sample);

  return img;
}

int webcam_platform_get_dimensions(webcam_context_t *ctx, int *width, int *height) {
  if (!ctx || !width || !height)
    return -1;

  *width = ctx->width;
  *height = ctx->height;
  return 0;
}


#endif