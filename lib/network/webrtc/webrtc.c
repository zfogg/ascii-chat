/**
 * @file networking/webrtc/webrtc.c
 * @brief WebRTC peer connection implementation using libdatachannel
 * @ingroup webrtc
 *
 * Star topology: Session creator = server, others = clients
 * - Server: Creates local offer, waits for client answers
 * - Client: Receives remote offer, creates answer
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/init.h>

#include <string.h>
#include <rtc/rtc.h>

// ============================================================================
// Internal Structures
// ============================================================================

struct webrtc_peer_connection {
  int rtc_id;                ///< libdatachannel peer connection ID
  webrtc_config_t config;    ///< Configuration with callbacks
  webrtc_state_t state;      ///< Current connection state
  webrtc_data_channel_t *dc; ///< Primary data channel (if created/received)
};

struct webrtc_data_channel {
  int rtc_id;                   ///< libdatachannel data channel ID
  webrtc_peer_connection_t *pc; ///< Parent peer connection
  bool is_open;                 ///< Channel open state

  // Per-channel callbacks (set via webrtc_datachannel_set_callbacks)
  void (*user_on_open)(webrtc_data_channel_t *dc, void *user_data);
  void (*user_on_close)(webrtc_data_channel_t *dc, void *user_data);
  void (*user_on_error)(webrtc_data_channel_t *dc, const char *error, void *user_data);
  void (*user_on_message)(webrtc_data_channel_t *dc, const uint8_t *data, size_t len, void *user_data);
  void *user_data; ///< User data for per-channel callbacks
};

// ============================================================================
// Global State
// ============================================================================

// WebRTC library reference counting (same pattern as PortAudio)
// Supports multiple init/cleanup pairs from different code paths
static unsigned int g_webrtc_init_refcount = 0;
static static_mutex_t g_webrtc_refcount_mutex = STATIC_MUTEX_INIT;

// ============================================================================
// libdatachannel Callback Adapters
// ============================================================================

static void rtc_log_callback(rtcLogLevel level, const char *message) {
  switch (level) {
  case RTC_LOG_FATAL:
  case RTC_LOG_ERROR:
    log_error("[libdatachannel] %s", message);
    break;
  case RTC_LOG_WARNING:
    log_warn("[libdatachannel] %s", message);
    break;
  case RTC_LOG_INFO:
    log_info("[libdatachannel] %s", message);
    break;
  case RTC_LOG_DEBUG:
  case RTC_LOG_VERBOSE:
    log_debug("[libdatachannel] %s", message);
    break;
  default:
    break;
  }
}

// Forward declarations for callback functions
static void on_datachannel_open_adapter(int dc_id, void *user_data);
static void on_datachannel_closed_adapter(int dc_id, void *user_data);
static void on_datachannel_message_adapter(int dc_id, const char *data, int size, void *user_data);
static void on_datachannel_error_adapter(int dc_id, const char *error, void *user_data);

static void on_state_change_adapter(int pc_id, rtcState state, void *user_data) {
  (void)pc_id; // Unused - we get peer connection from user_data
  webrtc_peer_connection_t *pc = (webrtc_peer_connection_t *)user_data;
  if (!pc)
    return;

  // Map libdatachannel state to our enum
  webrtc_state_t new_state;
  switch (state) {
  case RTC_NEW:
    new_state = WEBRTC_STATE_NEW;
    break;
  case RTC_CONNECTING:
    new_state = WEBRTC_STATE_CONNECTING;
    break;
  case RTC_CONNECTED:
    new_state = WEBRTC_STATE_CONNECTED;
    break;
  case RTC_DISCONNECTED:
    new_state = WEBRTC_STATE_DISCONNECTED;
    break;
  case RTC_FAILED:
    new_state = WEBRTC_STATE_FAILED;
    break;
  case RTC_CLOSED:
    new_state = WEBRTC_STATE_CLOSED;
    break;
  default:
    new_state = WEBRTC_STATE_NEW;
    break;
  }

  pc->state = new_state;

  if (pc->config.on_state_change) {
    pc->config.on_state_change(pc, new_state, pc->config.user_data);
  }
}

static void on_local_description_adapter(int pc_id, const char *sdp, const char *type, void *user_data) {
  (void)pc_id; // Unused - we get peer connection from user_data
  webrtc_peer_connection_t *pc = (webrtc_peer_connection_t *)user_data;
  if (!pc)
    return;

  if (pc->config.on_local_description) {
    pc->config.on_local_description(pc, sdp, type, pc->config.user_data);
  }
}

static void on_local_candidate_adapter(int pc_id, const char *candidate, const char *mid, void *user_data) {
  (void)pc_id; // Unused - we get peer connection from user_data
  webrtc_peer_connection_t *pc = (webrtc_peer_connection_t *)user_data;
  if (!pc)
    return;

  if (pc->config.on_local_candidate) {
    pc->config.on_local_candidate(pc, candidate, mid, pc->config.user_data);
  }
}

static void on_datachannel_adapter(int pc_id, int dc_id, void *user_data) {
  (void)pc_id; // Unused - we get peer connection from user_data
  log_info("on_datachannel_adapter: pc_id=%d, dc_id=%d, user_data=%p", pc_id, dc_id, user_data);

  webrtc_peer_connection_t *pc = (webrtc_peer_connection_t *)user_data;
  if (!pc) {
    log_error("on_datachannel_adapter: peer connection user_data is NULL!");
    return;
  }

  log_info("on_datachannel_adapter: received DataChannel (dc_id=%d) from remote peer", dc_id);

  // Allocate data channel wrapper
  webrtc_data_channel_t *dc = SAFE_MALLOC(sizeof(webrtc_data_channel_t), webrtc_data_channel_t *);
  if (!dc) {
    log_error("Failed to allocate data channel wrapper");
    rtcDeleteDataChannel(dc_id);
    return;
  }

  dc->rtc_id = dc_id;
  dc->pc = pc;
  dc->is_open = false;
  log_debug("Initialized DataChannel wrapper: dc=%p, is_open=false", (void *)dc);

  // Store in peer connection
  pc->dc = dc;

  // Set up data channel callbacks for incoming channel
  rtcSetUserPointer(dc_id, dc);
  rtcSetOpenCallback(dc_id, on_datachannel_open_adapter);
  rtcSetMessageCallback(dc_id, on_datachannel_message_adapter);
  rtcSetErrorCallback(dc_id, on_datachannel_error_adapter);

  log_info("Set up callbacks for incoming DataChannel (dc_id=%d, dc=%p)", dc_id, (void *)dc);

  // Check if DataChannel is already open (can happen if it opened before callbacks were set)
  // In libdatachannel, negotiated DataChannels may be open immediately when received
  // We manually trigger the open callback if that's the case
  if (rtcIsOpen(dc_id)) {
    log_info("DataChannel was already open when received, manually triggering open callback");
    on_datachannel_open_adapter(dc_id, dc);
  }
}

static void on_datachannel_open_adapter(int dc_id, void *user_data) {
  webrtc_data_channel_t *dc = (webrtc_data_channel_t *)user_data;
  log_info("on_datachannel_open_adapter called: dc_id=%d, dc=%p", dc_id, (void *)dc);
  if (!dc) {
    log_error("on_datachannel_open_adapter: dc is NULL!");
    return;
  }

  dc->is_open = true;
  log_info("DataChannel opened (id=%d, dc=%p), set is_open=true", dc_id, (void *)dc);

  // Check per-channel callback first, then fall back to peer connection callback
  if (dc->user_on_open) {
    log_debug("Calling per-channel on_open callback");
    dc->user_on_open(dc, dc->user_data);
  } else if (dc->pc && dc->pc->config.on_datachannel_open) {
    log_debug("Calling user on_datachannel_open callback");
    dc->pc->config.on_datachannel_open(dc, dc->pc->config.user_data);
  } else {
    log_warn("No user on_datachannel_open callback registered");
  }
}

static void on_datachannel_closed_adapter(int dc_id, void *user_data) {
  webrtc_data_channel_t *dc = (webrtc_data_channel_t *)user_data;
  log_info("on_datachannel_closed_adapter called: dc_id=%d, dc=%p", dc_id, (void *)dc);
  if (!dc) {
    log_error("on_datachannel_closed_adapter: dc is NULL!");
    return;
  }

  dc->is_open = false;
  log_info("DataChannel closed (id=%d, dc=%p), set is_open=false", dc_id, (void *)dc);

  // Check per-channel callback first
  if (dc->user_on_close) {
    log_debug("Calling per-channel on_close callback");
    dc->user_on_close(dc, dc->user_data);
  }
}

static void on_datachannel_message_adapter(int dc_id, const char *data, int size, void *user_data) {
  (void)dc_id; // Unused - we get data channel from user_data

  // Log at libdatachannel callback level (debug level for normal operation)
  if (size >= 20 && data) {
    const uint8_t *pkt = (const uint8_t *)data;
    log_debug("★ LIBDATACHANNEL_RX: dc_id=%d, size=%d, first_20_bytes: %02x%02x%02x%02x %02x%02x%02x%02x "
              "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
              dc_id, size, pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[6], pkt[7], pkt[8], pkt[9], pkt[10],
              pkt[11], pkt[12], pkt[13], pkt[14], pkt[15], pkt[16], pkt[17], pkt[18], pkt[19]);
  } else {
    log_debug("★ LIBDATACHANNEL_RX: dc_id=%d, size=%d (no data or too small)", dc_id, size);
  }

  webrtc_data_channel_t *dc = (webrtc_data_channel_t *)user_data;
  if (!dc) {
    log_debug("★ LIBDATACHANNEL_RX: dc=NULL, dropping message");
    return;
  }

  // Check per-channel callback first, then fall back to peer connection callback
  if (dc->user_on_message) {
    dc->user_on_message(dc, (const uint8_t *)data, (size_t)size, dc->user_data);
  } else if (dc->pc && dc->pc->config.on_datachannel_message) {
    dc->pc->config.on_datachannel_message(dc, (const uint8_t *)data, (size_t)size, dc->pc->config.user_data);
  }
}

static void on_datachannel_error_adapter(int dc_id, const char *error, void *user_data) {
  webrtc_data_channel_t *dc = (webrtc_data_channel_t *)user_data;
  if (!dc)
    return;

  log_error("DataChannel error (id=%d): %s", dc_id, error);

  // Check per-channel callback first, then fall back to peer connection callback
  if (dc->user_on_error) {
    dc->user_on_error(dc, error, dc->user_data);
  } else if (dc->pc && dc->pc->config.on_datachannel_error) {
    dc->pc->config.on_datachannel_error(dc, error, dc->pc->config.user_data);
  }
}

// ============================================================================
// Initialization and Cleanup
// ============================================================================

/**
 * @brief Ensure WebRTC library is initialized with reference counting
 *
 * Supports multiple init/cleanup pairs from different code paths.
 * rtcInitLogger() and rtcPreload() are called exactly once.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t webrtc_ensure_initialized(void) {
  static_mutex_lock(&g_webrtc_refcount_mutex);

  // If already initialized, just increment refcount
  if (g_webrtc_init_refcount > 0) {
    g_webrtc_init_refcount++;
    static_mutex_unlock(&g_webrtc_refcount_mutex);
    return ASCIICHAT_OK;
  }

  // First initialization - call library initialization exactly once
  rtcInitLogger(RTC_LOG_INFO, rtc_log_callback);
  rtcPreload();

  g_webrtc_init_refcount = 1;
  static_mutex_unlock(&g_webrtc_refcount_mutex);

  log_debug("WebRTC library initialized (libdatachannel)");
  return ASCIICHAT_OK;
}

/**
 * @brief Release WebRTC, cleaning up when refcount reaches zero
 *
 * Counterpart to webrtc_ensure_initialized().
 * Decrements refcount and calls rtcCleanup() only when it reaches zero.
 */
static void webrtc_release(void) {
  static_mutex_lock(&g_webrtc_refcount_mutex);
  if (g_webrtc_init_refcount > 0) {
    g_webrtc_init_refcount--;
    if (g_webrtc_init_refcount == 0) {
      rtcCleanup();
      log_info("WebRTC library cleaned up");
    }
  }
  static_mutex_unlock(&g_webrtc_refcount_mutex);
}

asciichat_error_t webrtc_init(void) {
  return webrtc_ensure_initialized();
}

void webrtc_destroy(void) {
  webrtc_release();
}

// ============================================================================
// Peer Connection Management
// ============================================================================

asciichat_error_t webrtc_create_peer_connection(const webrtc_config_t *config, webrtc_peer_connection_t **pc_out) {
  if (!config || !pc_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid config or output parameter");
  }

  static_mutex_lock(&g_webrtc_refcount_mutex);
  bool is_initialized = (g_webrtc_init_refcount > 0);
  static_mutex_unlock(&g_webrtc_refcount_mutex);

  if (!is_initialized) {
    return SET_ERRNO(ERROR_INIT, "WebRTC library not initialized");
  }

  // Allocate peer connection wrapper
  webrtc_peer_connection_t *pc = SAFE_MALLOC(sizeof(webrtc_peer_connection_t), webrtc_peer_connection_t *);
  if (!pc) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate peer connection");
  }

  pc->config = *config; // Copy config
  pc->state = WEBRTC_STATE_NEW;
  pc->dc = NULL;

  // Build ICE server list for libdatachannel
  const char **ice_servers = NULL;
  size_t ice_count = 0;

  if (config->stun_count > 0 || config->turn_count > 0) {
    ice_count = config->stun_count + config->turn_count;
    ice_servers = SAFE_MALLOC(ice_count * sizeof(char *), const char **);
    if (!ice_servers) {
      SAFE_FREE(pc);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ICE server list");
    }

    // Add STUN servers
    for (size_t i = 0; i < config->stun_count; i++) {
      ice_servers[i] = config->stun_servers[i].host;
    }

    // Add TURN servers
    for (size_t i = 0; i < config->turn_count; i++) {
      ice_servers[config->stun_count + i] = config->turn_servers[i].url;
    }
  }

  // Create libdatachannel configuration
  rtcConfiguration rtc_config;
  memset(&rtc_config, 0, sizeof(rtc_config));
  rtc_config.iceServers = ice_servers;
  rtc_config.iceServersCount = (int)ice_count;
  rtc_config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;

  // Create peer connection
  int pc_id = rtcCreatePeerConnection(&rtc_config);

  // Free ICE server list (libdatachannel makes a copy)
  if (ice_servers) {
    SAFE_FREE(ice_servers);
  }

  if (pc_id < 0) {
    SAFE_FREE(pc);
    return SET_ERRNO(ERROR_NETWORK, "Failed to create peer connection (rtc error %d)", pc_id);
  }

  pc->rtc_id = pc_id;

  // Set up callbacks
  rtcSetUserPointer(pc_id, pc);
  rtcSetStateChangeCallback(pc_id, on_state_change_adapter);
  rtcSetLocalDescriptionCallback(pc_id, on_local_description_adapter);
  rtcSetLocalCandidateCallback(pc_id, on_local_candidate_adapter);
  rtcSetDataChannelCallback(pc_id, on_datachannel_adapter);

  *pc_out = pc;
  log_debug("Created WebRTC peer connection (id=%d)", pc_id);
  return ASCIICHAT_OK;
}

void webrtc_close_peer_connection(webrtc_peer_connection_t *pc) {
  if (!pc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer connection");
    return;
  }

  // Close data channel if exists
  if (pc->dc) {
    webrtc_close_datachannel(pc->dc);
    pc->dc = NULL;
  }

  // Close peer connection
  rtcDeletePeerConnection(pc->rtc_id);
  log_debug("Closed WebRTC peer connection (id=%d)", pc->rtc_id);

  SAFE_FREE(pc);
}

webrtc_state_t webrtc_get_state(webrtc_peer_connection_t *pc) {
  if (!pc) {
    log_warn("Peer connection is NULL");
    return WEBRTC_STATE_CLOSED;
  }
  return pc->state;
}

void *webrtc_get_user_data(webrtc_peer_connection_t *pc) {
  if (!pc) {
    log_warn("Peer connection is NULL");
    return NULL;
  }
  return pc->config.user_data;
}

// ============================================================================
// SDP Offer/Answer Exchange
// ============================================================================

asciichat_error_t webrtc_create_offer(webrtc_peer_connection_t *pc) {
  if (!pc) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer connection");
  }

  // Set local description with NULL type to trigger offer generation
  int result = rtcSetLocalDescription(pc->rtc_id, NULL);
  if (result != RTC_ERR_SUCCESS) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to create SDP offer (rtc error %d)", result);
  }

  log_debug("Creating SDP offer (pc_id=%d)", pc->rtc_id);
  return ASCIICHAT_OK;
}

asciichat_error_t webrtc_set_remote_description(webrtc_peer_connection_t *pc, const char *sdp, const char *type) {
  if (!pc || !sdp || !type) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  int result = rtcSetRemoteDescription(pc->rtc_id, sdp, type);
  if (result != RTC_ERR_SUCCESS) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to set remote SDP (rtc error %d)", result);
  }

  log_debug("Set remote SDP description (pc_id=%d, type=%s)", pc->rtc_id, type);
  return ASCIICHAT_OK;
}

// ============================================================================
// ICE Candidate Exchange
// ============================================================================

asciichat_error_t webrtc_add_remote_candidate(webrtc_peer_connection_t *pc, const char *candidate, const char *mid) {
  if (!pc || !candidate) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  log_debug("  [4] Before libdatachannel - candidate: '%s' (len=%zu)", candidate, strlen(candidate));
  log_debug("  [4] Before libdatachannel - mid: '%s' (len=%zu)", mid ? mid : "(null)", mid ? strlen(mid) : 0);

  int result = rtcAddRemoteCandidate(pc->rtc_id, candidate, mid);
  if (result != RTC_ERR_SUCCESS) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to add remote ICE candidate (rtc error %d)", result);
  }

  log_debug("Added remote ICE candidate (pc_id=%d)", pc->rtc_id);
  return ASCIICHAT_OK;
}

// ============================================================================
// DataChannel Management
// ============================================================================

asciichat_error_t webrtc_create_datachannel(webrtc_peer_connection_t *pc, const char *label,
                                            webrtc_data_channel_t **dc_out) {
  if (!pc || !label || !dc_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  // Create data channel
  int dc_id = rtcCreateDataChannel(pc->rtc_id, label);
  if (dc_id < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to create data channel (rtc error %d)", dc_id);
  }

  // Allocate wrapper
  webrtc_data_channel_t *dc = SAFE_MALLOC(sizeof(webrtc_data_channel_t), webrtc_data_channel_t *);
  if (!dc) {
    rtcDeleteDataChannel(dc_id);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate data channel wrapper");
  }

  dc->rtc_id = dc_id;
  dc->pc = pc;
  dc->is_open = false;

  // Set up callbacks
  rtcSetUserPointer(dc_id, dc);
  rtcSetOpenCallback(dc_id, on_datachannel_open_adapter);
  rtcSetMessageCallback(dc_id, on_datachannel_message_adapter);
  rtcSetErrorCallback(dc_id, on_datachannel_error_adapter);

  pc->dc = dc;
  *dc_out = dc;

  log_debug("Created DataChannel '%s' (dc_id=%d, pc_id=%d)", label, dc_id, pc->rtc_id);
  return ASCIICHAT_OK;
}

asciichat_error_t webrtc_datachannel_send(webrtc_data_channel_t *dc, const uint8_t *data, size_t size) {
  if (!dc || !data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  if (!dc->is_open) {
    log_error("★ WEBRTC_DATACHANNEL_SEND: Channel not open! size=%zu, dc->rtc_id=%d", size, dc ? dc->rtc_id : -1);
    return SET_ERRNO(ERROR_NETWORK, "DataChannel not open");
  }

  // Log packet details at network layer (debug level for normal operation)
  if (size >= 20) {
    const uint8_t *pkt = (const uint8_t *)data;
    log_debug("★ RTCSENDMESSAGE_BEFORE: dc_id=%d, size=%zu, first_20_bytes: %02x%02x%02x%02x %02x%02x%02x%02x "
              "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
              dc->rtc_id, size, pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[6], pkt[7], pkt[8], pkt[9], pkt[10],
              pkt[11], pkt[12], pkt[13], pkt[14], pkt[15], pkt[16], pkt[17], pkt[18], pkt[19]);
  } else {
    log_debug("★ RTCSENDMESSAGE_BEFORE: dc_id=%d, size=%zu (too small to log content)", dc->rtc_id, size);
  }

  int result = rtcSendMessage(dc->rtc_id, (const char *)data, (int)size);

  log_debug("★ RTCSENDMESSAGE_AFTER: dc_id=%d, rtcSendMessage returned %d for size=%zu", dc->rtc_id, result, size);

  if (result < 0) {
    log_error("★ WEBRTC_DATACHANNEL_SEND: FAILED with error code %d", result);
    return SET_ERRNO(ERROR_NETWORK, "Failed to send data (rtc error %d)", result);
  }

  log_debug("★ WEBRTC_DATACHANNEL_SEND: SUCCESS - sent %zu bytes", size);
  return ASCIICHAT_OK;
}

bool webrtc_datachannel_is_open(webrtc_data_channel_t *dc) {
  if (!dc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "DataChannel is NULL");
    return false;
  }
  return dc->is_open;
}

void webrtc_datachannel_set_open_state(webrtc_data_channel_t *dc, bool is_open) {
  if (!dc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "DataChannel is NULL");
    return;
  }
  dc->is_open = is_open;
}

const char *webrtc_datachannel_get_label(webrtc_data_channel_t *dc) {
  if (!dc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "DataChannel is NULL");
    return NULL;
  }

  static char label[256];
  int result = rtcGetDataChannelLabel(dc->rtc_id, label, sizeof(label));
  if (result < 0) {
    return NULL;
  }

  return label;
}

void webrtc_close_datachannel(webrtc_data_channel_t *dc) {
  if (!dc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "DataChannel is NULL");
    return;
  }

  // Save the ID before freeing (dc might be partially freed already)
  int dc_id = dc->rtc_id;

  rtcDeleteDataChannel(dc_id);
  log_debug("Closed DataChannel (dc_id=%d)", dc_id);

  SAFE_FREE(dc);
}

// =============================================================================
// DataChannel Callbacks
// =============================================================================

asciichat_error_t webrtc_datachannel_set_callbacks(webrtc_data_channel_t *dc,
                                                   const webrtc_datachannel_callbacks_t *callbacks) {
  if (!dc) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "DataChannel is NULL");
  }

  if (!callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Callbacks struct is NULL");
  }

  // Store user callbacks in the data channel struct
  dc->user_on_open = callbacks->on_open;
  dc->user_on_close = callbacks->on_close;
  dc->user_on_error = callbacks->on_error;
  dc->user_on_message = callbacks->on_message;
  dc->user_data = callbacks->user_data;

  // Register adapter functions with libdatachannel (adapters use correct signature)
  // The adapters will look up our stored callbacks and invoke them with proper types
  if (callbacks->on_open) {
    rtcSetOpenCallback(dc->rtc_id, on_datachannel_open_adapter);
  }

  if (callbacks->on_close) {
    rtcSetClosedCallback(dc->rtc_id, on_datachannel_closed_adapter);
  }

  if (callbacks->on_error) {
    rtcSetErrorCallback(dc->rtc_id, on_datachannel_error_adapter);
  }

  if (callbacks->on_message) {
    rtcSetMessageCallback(dc->rtc_id, on_datachannel_message_adapter);
  }

  // Set user pointer to the data channel so adapters can retrieve it
  rtcSetUserPointer(dc->rtc_id, dc);

  log_debug("Set DataChannel callbacks (dc_id=%d)", dc->rtc_id);
  return ASCIICHAT_OK;
}

void webrtc_datachannel_destroy(webrtc_data_channel_t *dc) {
  if (!dc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid null data channel");
    return;
  }

  // Close if still open, then free
  webrtc_close_datachannel(dc);
  // Note: webrtc_close_datachannel already calls SAFE_FREE(dc), so we're done
}

// =============================================================================
// Peer Connection Lifecycle
// =============================================================================

void webrtc_peer_connection_close(webrtc_peer_connection_t *pc) {
  if (!pc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid null peer connectiont");
    return;
  }

  rtcClose(pc->rtc_id);
  log_debug("Closed peer connection (pc_id=%d)", pc->rtc_id);
}

void webrtc_peer_connection_destroy(webrtc_peer_connection_t *pc) {
  if (!pc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid null peer connectiont");
    return;
  }

  // Close and delete peer connection
  rtcDeletePeerConnection(pc->rtc_id);
  log_debug("Destroyed peer connection (pc_id=%d)", pc->rtc_id);

  SAFE_FREE(pc);
}

/**
 * @brief Get the internal libdatachannel peer connection ID
 *
 * Helper function for C++ code that needs access to internal rtc_id
 * without exposing the full structure definition.
 *
 * @param pc Peer connection
 * @return libdatachannel peer connection ID, or -1 if pc is NULL
 */
int webrtc_get_rtc_id(webrtc_peer_connection_t *pc) {
  if (!pc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid null peer connectiont");
    return -1;
  }
  return pc->rtc_id;
}
