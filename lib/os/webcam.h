#pragma once

/**
 * @defgroup os OS Abstractions
 *
 * @defgroup webcam Webcam Module
 * @ingroup os
 * @brief 📷 Cross-platform webcam capture API
 */

/**
 * @file os/webcam.h
 * @ingroup webcam
 * @brief Operating system specific functionality (webcam capture, etc.)
 *
 * This header provides a cross-platform webcam capture interface for ascii-chat.
 * The system abstracts platform-specific webcam APIs (Windows Media Foundation,
 * Linux V4L2, macOS AVFoundation) behind a unified interface for video frame capture.
 *
 * CORE FEATURES:
 * ==============
 * - Cross-platform webcam access (Windows, Linux, macOS)
 * - Device enumeration and selection
 * - Real-time video frame capture
 * - Automatic format conversion to RGB
 * - Thread-safe context management
 * - Error handling with helpful diagnostics
 *
 * PLATFORM SUPPORT:
 * ==================
 * - Windows: Media Foundation API
 * - Linux: Video4Linux2 (V4L2)
 * - macOS: AVFoundation framework
 *
 * ARCHITECTURE:
 * =============
 * The webcam system uses a context-based architecture:
 * - Global context for simple single-webcam scenarios
 * - Per-context management for multi-webcam support
 * - Automatic format detection and conversion
 * - Frame rate management and throttling
 *
 * VIDEO FORMATS:
 * ==============
 * The system supports:
 * - Various native formats (YUY2, RGB, MJPEG, etc.)
 * - Automatic conversion to RGB for processing
 * - Format negotiation with webcam hardware
 * - Frame rate configuration
 *
 * @note The global webcam interface (webcam_init, webcam_read) provides
 *       simple single-webcam access. Use context-based functions for
 *       advanced multi-webcam scenarios.
 * @note Webcam frames are returned as image_t structures compatible
 *       with the image2ascii conversion pipeline.
 * @note Error codes include specific diagnostics for webcam issues
 *       (permission denied, device in use, etc.).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date August 2025
 */

#include <stdint.h>
#include "image2ascii/image.h"

/* ============================================================================
 * Device Information
 * @{
 */

/**
 * @brief Maximum length of webcam device name
 * @ingroup webcam
 */
#define WEBCAM_DEVICE_NAME_MAX 256

/**
 * @brief Webcam device information structure
 *
 * Contains information about an available webcam device. Used by device
 * enumeration functions to return a list of available devices.
 *
 * @note Device index corresponds to the device_index parameter in webcam_init().
 *
 * @ingroup webcam
 */
typedef struct {
  unsigned int index;                ///< Device index (use with webcam_init)
  char name[WEBCAM_DEVICE_NAME_MAX]; ///< Human-readable device name
} webcam_device_info_t;

/**
 * @brief Enumerate available webcam devices
 * @param devices Output pointer to array of device info structures (must not be NULL)
 * @param count Output pointer to number of devices found (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Enumerates all available webcam devices and returns their information.
 * The devices array is allocated by this function and must be freed with
 * webcam_free_device_list().
 *
 * @note On success, *devices will point to a dynamically allocated array.
 * @note If no devices are found, *count will be 0 and *devices will be NULL.
 * @note Call webcam_free_device_list() to free the returned array.
 *
 * Example:
 * @code
 * webcam_device_info_t *devices = NULL;
 * unsigned int count = 0;
 * if (webcam_list_devices(&devices, &count) == ASCIICHAT_OK) {
 *     for (unsigned int i = 0; i < count; i++) {
 *         printf("Device %u: %s\n", devices[i].index, devices[i].name);
 *     }
 *     webcam_free_device_list(devices);
 * }
 * @endcode
 *
 * @ingroup webcam
 */
asciichat_error_t webcam_list_devices(webcam_device_info_t **devices, unsigned int *count);

/**
 * @brief Free device list returned by webcam_list_devices()
 * @param devices Device list to free (can be NULL)
 *
 * Frees a device list allocated by webcam_list_devices(). Safe to call
 * with NULL pointer (no-op).
 *
 * @ingroup webcam
 */
void webcam_free_device_list(webcam_device_info_t *devices);

/** @} */

/* ============================================================================
 * High-Level Webcam Interface
 * @{
 */

/**
 * @brief Opaque webcam context structure
 *
 * Forward declaration of webcam context for context-based operations.
 * Context provides per-webcam state management for advanced scenarios.
 *
 * @note Use webcam_init_context() to create a new context.
 * @note Context must be cleaned up with webcam_cleanup_context().
 *
 * @ingroup webcam
 */
typedef struct webcam_context_t webcam_context_t;

/**
 * @brief Initialize global webcam interface
 * @param webcam_index Webcam device index (0 for default device)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes the global webcam interface for simple single-webcam access.
 * Opens the specified webcam device and prepares it for frame capture.
 * This is a convenience wrapper around webcam_init_context() for simple
 * single-webcam scenarios.
 *
 * @note This function initializes global webcam state.
 * @note Use webcam_read() to capture frames after initialization.
 * @note Must call webcam_cleanup() when done.
 * @note Use webcam_init_context() for advanced multi-webcam scenarios.
 *
 * @warning On failure, use webcam_print_init_error_help() for diagnostics.
 *
 * @ingroup webcam
 */
asciichat_error_t webcam_init(unsigned short int webcam_index);

/**
 * @brief Capture a frame from global webcam
 * @return Pointer to captured image, or NULL on error
 *
 * Captures a single video frame from the global webcam interface.
 * Returns an image_t structure containing the frame data in RGB format.
 * The image is automatically converted from native webcam format to RGB.
 *
 * @note Returns NULL on error (device disconnected, I/O error, etc.).
 * @note Image structure is allocated internally and must NOT be freed by caller.
 * @note Subsequent calls reuse the same buffer (frame overwrites previous frame).
 * @note Frame rate is limited by webcam hardware and format negotiation.
 *
 * @note For context-based operations, use webcam_read_context() instead.
 *
 * @ingroup webcam
 */
image_t *webcam_read(void);

/**
 * @brief Clean up global webcam interface
 *
 * Cleans up the global webcam interface and releases resources.
 * Closes the webcam device and frees all associated memory.
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note After cleanup, webcam_read() will fail until webcam_init() is called again.
 *
 * @ingroup webcam
 */
void webcam_cleanup(void);

/**
 * @brief Flush/interrupt any pending webcam read operations
 *
 * Cancels any blocking ReadSample operations. Call this before stopping
 * the capture thread to allow it to exit cleanly.
 *
 * @ingroup webcam
 */
void webcam_flush(void);

/* ============================================================================
 * Error Handling Helpers
 * @{
 */

/**
 * @brief Print helpful error diagnostics for webcam initialization failures
 * @param error_code Error code from webcam_init() or webcam_init_context()
 *
 * Prints human-readable error diagnostics to help diagnose webcam
 * initialization failures. Includes platform-specific troubleshooting
 * advice for common issues (permission denied, device in use, etc.).
 *
 * @note This function prints to stderr with detailed diagnostics.
 * @note Useful for debugging webcam access issues.
 *
 * @ingroup webcam
 */
void webcam_print_init_error_help(asciichat_error_t error_code);

/** @} */

/* ============================================================================
 * Context-Based Webcam Interface
 * @{
 */

/**
 * @brief Initialize webcam context for advanced operations
 * @param ctx Output pointer to webcam context (must not be NULL)
 * @param device_index Webcam device index (0 for default device)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes a new webcam context for context-based webcam management.
 * This allows multiple webcams to be used simultaneously or provides
 * more control over webcam lifecycle. Context must be cleaned up with
 * webcam_cleanup_context() when done.
 *
 * @note Context is allocated by this function and must be freed with
 *       webcam_cleanup_context().
 * @note Use webcam_read_context() to capture frames from this context.
 * @note Use webcam_get_dimensions() to query frame dimensions.
 *
 * @warning On failure, use webcam_print_init_error_help() for diagnostics.
 *
 * @ingroup webcam
 */
asciichat_error_t webcam_init_context(webcam_context_t **ctx, unsigned short int device_index);

/**
 * @brief Clean up webcam context and release resources
 * @param ctx Webcam context to clean up (can be NULL)
 *
 * Cleans up a webcam context and releases all associated resources.
 * Closes the webcam device, frees memory, and invalidates the context.
 *
 * @note Safe to call multiple times or with NULL pointer (no-op).
 * @note After cleanup, context pointer is invalid and must not be used.
 *
 * @ingroup webcam
 */
void webcam_cleanup_context(webcam_context_t *ctx);

/**
 * @brief Flush/interrupt pending read operations on webcam context
 * @param ctx Webcam context (may be NULL - no-op)
 *
 * Cancels any blocking read operations. Call before stopping capture thread.
 *
 * @ingroup webcam
 */
void webcam_flush_context(webcam_context_t *ctx);

/**
 * @brief Capture a frame from webcam context
 * @param ctx Webcam context (must not be NULL)
 * @return Pointer to captured image, or NULL on error
 *
 * Captures a single video frame from the specified webcam context.
 * Returns an image_t structure containing the frame data in RGB format.
 * The image is automatically converted from native webcam format to RGB.
 *
 * @note Returns NULL on error (device disconnected, I/O error, etc.).
 * @note Image structure is allocated internally and must NOT be freed by caller.
 * @note Subsequent calls reuse the same buffer (frame overwrites previous frame).
 * @note Frame rate is limited by webcam hardware and format negotiation.
 *
 * @ingroup webcam
 */
image_t *webcam_read_context(webcam_context_t *ctx);

/**
 * @brief Get webcam frame dimensions
 * @param ctx Webcam context (must not be NULL)
 * @param width Output pointer for frame width (must not be NULL)
 * @param height Output pointer for frame height (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Queries the webcam context for current frame dimensions. Returns
 * the width and height in pixels as determined during format negotiation
 * with the webcam hardware.
 *
 * @note Dimensions may change if webcam format is renegotiated.
 * @note Frame dimensions are set during webcam_init_context().
 *
 * @ingroup webcam
 */
asciichat_error_t webcam_get_dimensions(webcam_context_t *ctx, int *width, int *height);

/** @} */
