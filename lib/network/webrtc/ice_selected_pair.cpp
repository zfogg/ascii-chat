/**
 * @file network/webrtc/ice_selected_pair.cpp
 * @brief C++ implementation for retrieving selected ICE candidate pair
 * @ingroup webrtc
 *
 * Uses libdatachannel's C++ API to access selected candidate pair information
 * which is not exposed in the C API.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "ice.h"
#include "webrtc.h"
#include "log/logging.h"

#include <rtc/rtc.hpp>
#include <string>
#include <cstring>

// Internal structure access (from webrtc.c)
// We need to access the rtc_id from the peer connection
extern "C" {
struct webrtc_peer_connection {
  int rtc_id;
  // ... other fields we don't need
};
}

/**
 * @brief Parse ICE candidate string from libdatachannel into ice_candidate_t
 *
 * libdatachannel returns candidates in format:
 * "candidate:foundation component protocol priority ip port typ type [raddr rport]"
 */
static asciichat_error_t parse_datachannel_candidate(const std::string &candidate_str, ice_candidate_t *candidate) {
  if (!candidate) {
    return ERROR_INVALID_PARAM;
  }

  // Remove "candidate:" prefix if present
  std::string line = candidate_str;
  const char *prefix = "candidate:";
  size_t prefix_len = strlen(prefix);
  if (line.compare(0, prefix_len, prefix) == 0) {
    line = line.substr(prefix_len);
  }

  // Use the existing ice_parse_candidate function
  return ice_parse_candidate(line.c_str(), candidate);
}

extern "C" {

/**
 * @brief Get selected ICE candidate pair (C++ implementation)
 *
 * This function uses libdatachannel's C++ API to retrieve the selected
 * candidate pair, which is not available in the C API.
 */
asciichat_error_t ice_get_selected_pair_impl(webrtc_peer_connection_t *pc, ice_candidate_t *local_candidate,
                                             ice_candidate_t *remote_candidate) {
  if (!pc) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Peer connection is NULL");
  }

  try {
    // Get the libdatachannel peer connection ID
    int rtc_id = pc->rtc_id;

    // Query selected candidate pair from libdatachannel
    // Note: This requires libdatachannel to have the selected candidate pair available
    char local_buf[512];
    char remote_buf[512];

    // Try to get the selected local candidate
    if (rtcGetSelectedCandidatePair(rtc_id, local_buf, sizeof(local_buf), remote_buf, sizeof(remote_buf)) < 0) {
      // No pair selected yet, or error accessing it
      return SET_ERRNO(ERROR_INVALID_STATE, "No candidate pair selected yet");
    }

    // Parse local candidate if requested
    if (local_candidate) {
      std::string local_str(local_buf);
      asciichat_error_t err = parse_datachannel_candidate(local_str, local_candidate);
      if (err != ASCIICHAT_OK) {
        return SET_ERRNO(err, "Failed to parse local candidate");
      }
    }

    // Parse remote candidate if requested
    if (remote_candidate) {
      std::string remote_str(remote_buf);
      asciichat_error_t err = parse_datachannel_candidate(remote_str, remote_candidate);
      if (err != ASCIICHAT_OK) {
        return SET_ERRNO(err, "Failed to parse remote candidate");
      }
    }

    return ASCIICHAT_OK;

  } catch (const std::exception &e) {
    return SET_ERRNO(ERROR_NETWORK, "Exception while retrieving candidate pair: %s", e.what());
  } catch (...) {
    return SET_ERRNO(ERROR_NETWORK, "Unknown exception while retrieving candidate pair");
  }
}

} // extern "C"
