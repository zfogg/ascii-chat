/**
 * @file client/capture.h
 * @ingroup client_capture
 * @brief ascii-chat Client Media Capture Management Interface
 *
 * Defines the interface for webcam video capture and transmission
 * management in the ascii-chat client.
 *
 * The capture subsystem manages platform-specific webcam access, frame acquisition,
 * and periodic transmission to the server. It also supports test pattern generation
 * and snapshot mode for testing without real hardware.
 *
 * ## Capture Lifecycle
 *
 * 1. **Initialization** (capture_init): Set up platform-specific webcam drivers
 * 2. **Thread Start** (capture_start_thread): Spawn capture thread in connection loop
 * 3. **Capture Loop**: Continuously grab frames and send to server
 * 4. **Thread Stop** (capture_stop_thread): Signal thread to exit and wait
 * 5. **Cleanup** (capture_cleanup): Release platform resources
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 *
 * @see topic_client_capture "Video Capture Architecture"
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Initialize capture subsystem
 *
 * Sets up platform-specific webcam drivers and enumerates available devices.
 * This must be called before any other capture functions.
 *
 * The initialization process:
 * 1. Detects platform (Linux/macOS/Windows)
 * 2. Loads platform webcam driver (V4L2/AVFoundation/Media Foundation)
 * 3. Enumerates available cameras
 * 4. Opens the selected webcam (index specified by --webcam-index)
 * 5. Initializes frame buffers and capture parameters
 *
 * **Options affecting initialization**:
 * - `--webcam-index <N>`: Which camera to open (default: 0, first camera)
 * - `--test-pattern`: Use synthetic test pattern instead of real webcam
 * - `--webcam-flip`: Flip video vertically
 *
 * @return 0 on success, negative on error:
 *         - ERROR_WEBCAM: Device not found or driver error
 *         - ERROR_WEBCAM_IN_USE: Device busy (another app using it)
 *
 * @note This is called during client initialization, before entering the connection loop.
 *
 * @ingroup client_capture
 *
 * @see capture_cleanup "Cleanup resources"
 */
int capture_init();

/**
 * @brief Start capture thread
 *
 * Spawns the capture thread in the global client worker thread pool.
 * The thread will run the main capture loop, continuously acquiring frames
 * from the webcam and transmitting them to the server.
 *
 * The capture loop:
 * 1. Grabs frame from webcam (blocking, rate-limited to target FPS)
 * 2. Optionally compresses frame if --compress is enabled
 * 3. Sends PACKET_TYPE_IMAGE_FRAME to server via send_packet_to_server()
 * 4. Checks if in snapshot mode and should exit
 * 5. Sleeps for frame interval to maintain target FPS
 * 6. Loops until exit signal or connection loss
 *
 * Must be called after server connection is established and protocol thread started.
 *
 * @return 0 on success, negative on error (thread pool full or system error)
 *
 * @note Must be paired with capture_stop_thread() to properly clean up.
 *
 * @ingroup client_capture
 *
 * @see capture_stop_thread "Stop capture thread"
 * @see capture_thread_exited "Check if thread has exited"
 */
int capture_start_thread();

/**
 * @brief Stop capture thread
 *
 * Signals the capture thread to exit and waits for it to join cleanly.
 * Called during connection loss handling or shutdown.
 *
 * The function:
 * 1. Sets shutdown flag
 * 2. Polls capture_thread_exited() until thread exits
 * 3. Releases thread-specific resources
 *
 * Must be called before reconnection to prevent resource leaks.
 *
 * @note This function blocks until the capture thread exits.
 *       Typical wait time: <1 second (one frame interval).
 *
 * @ingroup client_capture
 *
 * @see capture_start_thread "Start capture thread"
 */
void capture_stop_thread();

/**
 * @brief Check if capture thread has exited
 *
 * Atomically checks whether the capture thread has finished execution.
 * The main thread uses this to wait for clean thread shutdown before
 * reconnection or final cleanup.
 *
 * @return true if thread exited, false otherwise
 *
 * @note The main thread typically polls this in a 10-100ms loop until true.
 *
 * @ingroup client_capture
 *
 * @see capture_stop_thread "Stop capture thread"
 */
bool capture_thread_exited();

/**
 * @brief Cleanup capture subsystem
 *
 * Releases all platform resources associated with webcam access.
 * This must be called after capture_stop_thread() and before process exit.
 *
 * The cleanup process:
 * 1. Closes webcam device (platform-specific close)
 * 2. Frees frame buffers
 * 3. Releases platform driver resources
 * 4. Cleans up thread handles
 *
 * Must be called in proper order during shutdown to avoid resource leaks
 * and to restore the platform to a clean state.
 *
 * @ingroup client_capture
 *
 * @see capture_init "Initialize capture subsystem"
 */
void capture_cleanup();
