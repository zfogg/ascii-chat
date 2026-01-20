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

#include "network/webrtc/webrtc.h"
#include "common.h"
#include "log/logging.h"

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
};

// ============================================================================
// Global State
// ============================================================================

static bool g_webrtc_initialized = false;

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

  if (dc->pc && dc->pc->config.on_datachannel_open) {
    log_debug("Calling user on_datachannel_open callback");
    dc->pc->config.on_datachannel_open(dc, dc->pc->config.user_data);
  } else {
    log_warn("No user on_datachannel_open callback registered");
  }
}

static void on_datachannel_message_adapter(int dc_id, const char *data, int size, void *user_data) {
  (void)dc_id; // Unused - we get data channel from user_data
  webrtc_data_channel_t *dc = (webrtc_data_channel_t *)user_data;
  if (!dc)
    return;

  if (dc->pc && dc->pc->config.on_datachannel_message) {
    dc->pc->config.on_datachannel_message(dc, (const uint8_t *)data, (size_t)size, dc->pc->config.user_data);
  }
}

static void on_datachannel_error_adapter(int dc_id, const char *error, void *user_data) {
  webrtc_data_channel_t *dc = (webrtc_data_channel_t *)user_data;
  if (!dc)
    return;

  log_error("DataChannel error (id=%d): %s", dc_id, error);

  if (dc->pc && dc->pc->config.on_datachannel_error) {
    dc->pc->config.on_datachannel_error(dc, error, dc->pc->config.user_data);
  }
}

// ============================================================================
// Initialization and Cleanup
// ============================================================================

asciichat_error_t webrtc_init(void) {
  if (g_webrtc_initialized) {
    return ASCIICHAT_OK; // Already initialized
  }

  // Initialize libdatachannel logger
  rtcInitLogger(RTC_LOG_INFO, rtc_log_callback);

  // Preload global resources to avoid lazy-loading delays
  rtcPreload();

  g_webrtc_initialized = true;
  log_debug("WebRTC library initialized (libdatachannel)");
  return ASCIICHAT_OK;
}

void webrtc_cleanup(void) {
  if (!g_webrtc_initialized) {
    return;
  }

  rtcCleanup();
  g_webrtc_initialized = false;
  log_info("WebRTC library cleaned up");
}

// ============================================================================
// Peer Connection Management
// ============================================================================

asciichat_error_t webrtc_create_peer_connection(const webrtc_config_t *config, webrtc_peer_connection_t **pc_out) {
  if (!config || !pc_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid config or output parameter");
  }

  if (!g_webrtc_initialized) {
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
    return WEBRTC_STATE_CLOSED;
  }
  return pc->state;
}

void *webrtc_get_user_data(webrtc_peer_connection_t *pc) {
  if (!pc) {
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
    return SET_ERRNO(ERROR_NETWORK, "DataChannel not open");
  }

  int result = rtcSendMessage(dc->rtc_id, (const char *)data, (int)size);
  if (result < 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send data (rtc error %d)", result);
  }

  return ASCIICHAT_OK;
}

bool webrtc_datachannel_is_open(webrtc_data_channel_t *dc) {
  return dc && dc->is_open;
}

void webrtc_datachannel_set_open_state(webrtc_data_channel_t *dc, bool is_open) {
  if (dc) {
    dc->is_open = is_open;
  }
}

const char *webrtc_datachannel_get_label(webrtc_data_channel_t *dc) {
  if (!dc) {
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

  // Store callbacks in DataChannel structure
  // NOTE: Intentional function pointer casts for libdatachannel API compatibility
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
  if (callbacks->on_open) {
    rtcSetOpenCallback(dc->rtc_id, (rtcOpenCallbackFunc)callbacks->on_open);
  }

  if (callbacks->on_close) {
    rtcSetClosedCallback(dc->rtc_id, (rtcClosedCallbackFunc)callbacks->on_close);
  }

  if (callbacks->on_error) {
    rtcSetErrorCallback(dc->rtc_id, (rtcErrorCallbackFunc)callbacks->on_error);
  }

  if (callbacks->on_message) {
    rtcSetMessageCallback(dc->rtc_id, (rtcMessageCallbackFunc)callbacks->on_message);
  }
#pragma clang diagnostic pop

  // Store user_data for callbacks (libdatachannel passes this to callbacks)
  if (callbacks->user_data) {
    rtcSetUserPointer(dc->rtc_id, callbacks->user_data);
  }

  log_debug("Set DataChannel callbacks (dc_id=%d)", dc->rtc_id);
  return ASCIICHAT_OK;
}

void webrtc_datachannel_destroy(webrtc_data_channel_t *dc) {
  if (!dc) {
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
    return;
  }

  rtcClose(pc->rtc_id);
  log_debug("Closed peer connection (pc_id=%d)", pc->rtc_id);
}

void webrtc_peer_connection_destroy(webrtc_peer_connection_t *pc) {
  if (!pc) {
    return;
  }

  // Close and delete peer connection
  rtcDeletePeerConnection(pc->rtc_id);
  log_debug("Destroyed peer connection (pc_id=%d)", pc->rtc_id);

  SAFE_FREE(pc);
}
