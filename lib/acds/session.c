/**
 * @file acds/session.c
 * @brief 🎯 Session registry implementation (sharded rwlock)
 *
 * High-performance session management using sharded rwlocks + uthash:
 *   - 16 shards reduce lock contention under high concurrency
 *   - uthash provides O(1) lookups within each shard
 *   - Fine-grained per-entry locking for participant modifications
 *   - No external dependency (uses platform abstraction layer)
 *
 * Sessions are ephemeral (24-hour expiration) and stored in memory.
 */

#include "acds/session.h"
#include "acds/main.h"
#include "acds/strings.h"
#include "log/logging.h"
#include "network/webrtc/turn_credentials.h"
#include "platform/rwlock.h"
#include "util/fnv1a.h"
#include <string.h>
#include <time.h>
#include <sodium.h>

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Generate random UUID (v4)
 * @param uuid_out Output buffer for 16-byte UUID
 */
static void generate_uuid(uint8_t uuid_out[16]) {
  randombytes_buf(uuid_out, 16);

  // Set version to 4 (random UUID)
  uuid_out[6] = (uuid_out[6] & 0x0F) | 0x40;

  // Set variant to RFC4122
  uuid_out[8] = (uuid_out[8] & 0x3F) | 0x80;
}

/**
 * @brief Get current time in milliseconds
 * @return Unix timestamp in milliseconds
 */
static uint64_t get_current_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/**
 * @brief Verify password against hash
 * @param password Cleartext password
 * @param hash Stored hash (from hash_password)
 * @return true if password matches, false otherwise
 */
static bool verify_password(const char *password, const char *hash) {
  return crypto_pwhash_str_verify(hash, password, strlen(password)) == 0;
}

// ============================================================================
// Sharded Hash Table Helpers
// ============================================================================

/**
 * @brief Get shard index for a session string
 *
 * Uses FNV-1a hash to distribute sessions across shards evenly.
 *
 * @param session_string Session string (null-terminated)
 * @return Shard index (0 to SESSION_REGISTRY_NUM_SHARDS-1)
 */
static inline uint32_t get_shard_index(const char *session_string) {
  return fnv1a_hash_string(session_string) % SESSION_REGISTRY_NUM_SHARDS;
}

/**
 * @brief Find session by session_id (must hold appropriate shard locks)
 *
 * This iterates through all shards - caller must coordinate locking.
 * Used for operations that lookup by session_id rather than session_string.
 *
 * @param registry Session registry
 * @param session_id Session UUID
 * @param out_shard_idx Output: shard index where session was found (optional)
 * @return Session entry or NULL if not found
 */
static session_entry_t *find_session_by_id_unlocked(session_registry_t *registry, const uint8_t session_id[16],
                                                    uint32_t *out_shard_idx) {
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    session_entry_t *entry, *tmp;
    HASH_ITER(hh, registry->shards[i].sessions, entry, tmp) {
      if (memcmp(entry->session_id, session_id, 16) == 0) {
        if (out_shard_idx) {
          *out_shard_idx = i;
        }
        return entry;
      }
    }
  }
  return NULL;
}

/**
 * @brief Find participant in session (caller must hold participant_mutex)
 * @param session Session entry
 * @param participant_id Participant UUID
 * @return Participant or NULL if not found
 */
static participant_t *find_participant_locked(session_entry_t *session, const uint8_t participant_id[16]) {
  for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
    if (session->participants[i] && memcmp(session->participants[i]->participant_id, participant_id, 16) == 0) {
      return session->participants[i];
    }
  }
  return NULL;
}

/**
 * @brief Find empty participant slot in session (caller must hold participant_mutex)
 * @param session Session entry
 * @return Slot index or -1 if full
 */
static int find_empty_slot_locked(session_entry_t *session) {
  for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
    if (session->participants[i] == NULL) {
      return (int)i;
    }
  }
  return -1;
}

/**
 * @brief Free a session entry and all its resources
 * @param entry Session entry to free
 */
static void session_entry_free(session_entry_t *entry) {
  if (!entry) {
    return;
  }

  // Free all participants
  for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
    if (entry->participants[i]) {
      SAFE_FREE(entry->participants[i]);
    }
  }

  // Destroy per-entry mutex
  mutex_destroy(&entry->participant_mutex);

  // Free the entry itself
  SAFE_FREE(entry);
}

// ============================================================================
// Registry Lifecycle
// ============================================================================

asciichat_error_t session_registry_init(session_registry_t *registry) {
  if (!registry) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry is NULL");
  }

  memset(registry, 0, sizeof(*registry));

  // Initialize all shards
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    asciichat_error_t result = rwlock_init(&registry->shards[i].lock);
    if (result != ASCIICHAT_OK) {
      // Cleanup already initialized shards
      for (uint32_t j = 0; j < i; j++) {
        rwlock_destroy(&registry->shards[j].lock);
      }
      return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize shard %u rwlock", i);
    }
    registry->shards[i].sessions = NULL; // uthash starts with NULL
  }

  log_info("Session registry initialized (%d shards with rwlocks)", SESSION_REGISTRY_NUM_SHARDS);
  return ASCIICHAT_OK;
}

void session_registry_destroy(session_registry_t *registry) {
  if (!registry) {
    return;
  }

  int deleted_count = 0;

  // Destroy all shards
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    // Acquire write lock for this shard
    rwlock_wrlock(&registry->shards[i].lock);

    // Free all sessions in this shard
    session_entry_t *entry, *tmp;
    HASH_ITER(hh, registry->shards[i].sessions, entry, tmp) {
      HASH_DEL(registry->shards[i].sessions, entry);
      session_entry_free(entry);
      deleted_count++;
    }

    rwlock_wrunlock(&registry->shards[i].lock);
    rwlock_destroy(&registry->shards[i].lock);
  }

  log_debug("Deleted %d sessions during registry shutdown", deleted_count);
  log_info("Session registry destroyed");
}

// ============================================================================
// Session Operations
// ============================================================================

asciichat_error_t session_create(session_registry_t *registry, const acip_session_create_t *req,
                                 const acds_config_t *config, acip_session_created_t *resp) {
  if (!registry || !req || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, req, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));

  // Generate or use reserved session string
  char session_string[ACIP_MAX_SESSION_STRING_LEN] = {0};
  if (req->reserved_string_len > 0) {
    // Use provided string (copy from variable part after struct)
    const char *reserved_str = (const char *)(req + 1);
    size_t len = req->reserved_string_len < (ACIP_MAX_SESSION_STRING_LEN - 1) ? req->reserved_string_len
                                                                              : (ACIP_MAX_SESSION_STRING_LEN - 1);
    memcpy(session_string, reserved_str, len);
    session_string[len] = '\0';

    // Validate format
    if (!acds_string_validate(session_string)) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session string format: %s", session_string);
    }
  } else {
    // Auto-generate session string
    asciichat_error_t result = acds_string_generate(session_string, sizeof(session_string));
    if (result != ASCIICHAT_OK) {
      return result;
    }
  }

  // Determine which shard this session belongs to
  uint32_t shard_idx = get_shard_index(session_string);
  session_shard_t *shard = &registry->shards[shard_idx];

  // Allocate new session entry
  session_entry_t *session = SAFE_MALLOC(sizeof(session_entry_t), session_entry_t *);
  if (!session) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate session entry");
  }

  memset(session, 0, sizeof(*session));

  // Initialize per-entry mutex for participant list
  asciichat_error_t mutex_result = mutex_init(&session->participant_mutex);
  if (mutex_result != ASCIICHAT_OK) {
    SAFE_FREE(session);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize participant mutex");
  }

  // Fill session data
  SAFE_STRNCPY(session->session_string, session_string, sizeof(session->session_string));
  generate_uuid(session->session_id);
  memcpy(session->host_pubkey, req->identity_pubkey, 32);
  session->capabilities = req->capabilities;
  session->max_participants =
      req->max_participants > 0 && req->max_participants <= MAX_PARTICIPANTS ? req->max_participants : MAX_PARTICIPANTS;
  session->has_password = req->has_password != 0;
  session->current_participants = 0;

  // Server connection information
  SAFE_STRNCPY(session->server_address, req->server_address, sizeof(session->server_address));
  session->server_port = req->server_port;

  // Hash password if provided
  if (session->has_password) {
    memcpy(session->password_hash, req->password_hash, sizeof(session->password_hash));
  }

  // IP disclosure policy (explicit opt-in)
  session->expose_ip_publicly = req->expose_ip_publicly != 0;

  // Session type (Direct TCP or WebRTC)
  session->session_type = req->session_type;

  // Set timestamps
  uint64_t now = get_current_time_ms();
  session->created_at = now;
  session->expires_at = now + ACIP_SESSION_EXPIRATION_MS;

  // Acquire write lock for this shard
  rwlock_wrlock(&shard->lock);

  // Check if session_string already exists
  session_entry_t *existing = NULL;
  HASH_FIND_STR(shard->sessions, session_string, existing);
  if (existing) {
    rwlock_wrunlock(&shard->lock);
    mutex_destroy(&session->participant_mutex);
    SAFE_FREE(session);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session string already exists: %s", session_string);
  }

  // Add to hash table
  HASH_ADD_STR(shard->sessions, session_string, session);

  rwlock_wrunlock(&shard->lock);

  // Fill response
  resp->session_string_len = (uint8_t)strlen(session_string);
  SAFE_STRNCPY(resp->session_string, session_string, sizeof(resp->session_string));
  memcpy(resp->session_id, session->session_id, 16);
  resp->expires_at = session->expires_at;

  // Populate STUN/TURN server counts from config
  resp->stun_count = config->stun_count;
  resp->turn_count = config->turn_count;

  log_info("Session created: %s (max_participants=%d, has_password=%d, shard=%u)", session_string,
           session->max_participants, session->has_password, shard_idx);

  return ASCIICHAT_OK;
}

asciichat_error_t session_lookup(session_registry_t *registry, const char *session_string, const acds_config_t *config,
                                 acip_session_info_t *resp) {
  if (!registry || !session_string || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, session_string, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));

  // Determine which shard this session belongs to
  uint32_t shard_idx = get_shard_index(session_string);
  session_shard_t *shard = &registry->shards[shard_idx];

  // Acquire read lock for this shard
  rwlock_rdlock(&shard->lock);

  // Find session by string
  session_entry_t *session = NULL;
  HASH_FIND_STR(shard->sessions, session_string, session);

  if (!session) {
    rwlock_rdunlock(&shard->lock);
    resp->found = 0;
    log_debug("Session lookup failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Fill response - session data
  resp->found = 1;
  memcpy(resp->session_id, session->session_id, 16);
  memcpy(resp->host_pubkey, session->host_pubkey, 32);
  resp->capabilities = session->capabilities;
  resp->max_participants = session->max_participants;
  resp->current_participants = session->current_participants;
  resp->has_password = session->has_password;
  resp->created_at = session->created_at;
  resp->expires_at = session->expires_at;

  // Fill response - ACDS policy flags
  resp->require_server_verify = config->require_server_verify ? 1 : 0;
  resp->require_client_verify = config->require_client_verify ? 1 : 0;

  // Session type (Direct TCP or WebRTC)
  resp->session_type = session->session_type;

  // NOTE: Server connection information (IP/port) is NOT included in SESSION_INFO.
  // It is only revealed after successful authentication via SESSION_JOIN to prevent
  // IP address leakage to unauthenticated clients.

  rwlock_rdunlock(&shard->lock);

  log_debug("Session lookup: %s (found, participants=%d/%d)", session_string, resp->current_participants,
            resp->max_participants);

  return ASCIICHAT_OK;
}

asciichat_error_t session_join(session_registry_t *registry, const acip_session_join_t *req,
                               const acds_config_t *config, acip_session_joined_t *resp) {
  if (!registry || !req || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, req, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));
  resp->success = 0;

  // Extract session string (null-terminate)
  char session_string[ACIP_MAX_SESSION_STRING_LEN] = {0};
  size_t len = req->session_string_len < (ACIP_MAX_SESSION_STRING_LEN - 1) ? req->session_string_len
                                                                           : (ACIP_MAX_SESSION_STRING_LEN - 1);
  memcpy(session_string, req->session_string, len);
  session_string[len] = '\0';

  // Determine which shard this session belongs to
  uint32_t shard_idx = get_shard_index(session_string);
  session_shard_t *shard = &registry->shards[shard_idx];

  // Acquire read lock for this shard (we'll upgrade to write if needed via participant_mutex)
  rwlock_rdlock(&shard->lock);

  // Find session
  session_entry_t *session = NULL;
  HASH_FIND_STR(shard->sessions, session_string, session);

  if (!session) {
    rwlock_rdunlock(&shard->lock);
    resp->error_code = ACIP_ERROR_SESSION_NOT_FOUND;
    SAFE_STRNCPY(resp->error_message, "Session not found", sizeof(resp->error_message));
    log_warn("Session join failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Acquire fine-grained per-entry mutex for participant modifications
  mutex_lock(&session->participant_mutex);

  // Check if session full
  if (session->current_participants >= session->max_participants) {
    mutex_unlock(&session->participant_mutex);
    rwlock_rdunlock(&shard->lock);
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "Session is full", sizeof(resp->error_message));
    log_warn("Session join failed: %s (full)", session_string);
    return ASCIICHAT_OK;
  }

  // Verify password if required
  if (session->has_password && req->has_password) {
    if (!verify_password(req->password, session->password_hash)) {
      mutex_unlock(&session->participant_mutex);
      rwlock_rdunlock(&shard->lock);
      resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
      SAFE_STRNCPY(resp->error_message, "Invalid password", sizeof(resp->error_message));
      log_warn("Session join failed: %s (invalid password)", session_string);
      return ASCIICHAT_OK;
    }
  } else if (session->has_password && !req->has_password) {
    mutex_unlock(&session->participant_mutex);
    rwlock_rdunlock(&shard->lock);
    resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
    SAFE_STRNCPY(resp->error_message, "Password required", sizeof(resp->error_message));
    log_warn("Session join failed: %s (password required)", session_string);
    return ASCIICHAT_OK;
  }

  // Find empty participant slot
  int slot = find_empty_slot_locked(session);
  if (slot < 0) {
    mutex_unlock(&session->participant_mutex);
    rwlock_rdunlock(&shard->lock);
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "No participant slots available", sizeof(resp->error_message));
    log_error("Session join failed: %s (no slots, but count was not full)", session_string);
    return ASCIICHAT_OK;
  }

  // Allocate participant
  participant_t *participant = SAFE_MALLOC(sizeof(participant_t), participant_t *);
  if (!participant) {
    mutex_unlock(&session->participant_mutex);
    rwlock_rdunlock(&shard->lock);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate participant");
  }

  memset(participant, 0, sizeof(*participant));

  // Fill participant data
  generate_uuid(participant->participant_id);
  memcpy(participant->identity_pubkey, req->identity_pubkey, 32);
  participant->joined_at = get_current_time_ms();

  // Add participant to session
  session->participants[slot] = participant;
  session->current_participants++;

  // Fill response
  resp->success = 1;
  resp->error_code = ACIP_ERROR_NONE;
  memcpy(resp->participant_id, participant->participant_id, 16);
  memcpy(resp->session_id, session->session_id, 16);

  // Server connection information (CRITICAL SECURITY: Conditional IP disclosure)
  bool reveal_ip = false;

  if (session->has_password) {
    // Password was already verified
    reveal_ip = true;
  } else if (session->expose_ip_publicly) {
    // No password, but explicit opt-in via --acds-expose-ip
    reveal_ip = true;
  } else {
    log_warn("Session join: %s has no password and expose_ip_publicly=false - IP NOT REVEALED", session_string);
    reveal_ip = false;
  }

  if (reveal_ip) {
    SAFE_STRNCPY(resp->server_address, session->server_address, sizeof(resp->server_address));
    resp->server_port = session->server_port;
    resp->session_type = session->session_type;

    // Generate TURN credentials for WebRTC sessions
    if (session->session_type == SESSION_TYPE_WEBRTC && config->turn_secret[0] != '\0') {
      turn_credentials_t turn_creds;
      asciichat_error_t turn_result = turn_generate_credentials(session_string, config->turn_secret, 86400, // 24 hours
                                                                &turn_creds);
      if (turn_result == ASCIICHAT_OK) {
        SAFE_STRNCPY(resp->turn_username, turn_creds.username, sizeof(resp->turn_username));
        SAFE_STRNCPY(resp->turn_password, turn_creds.password, sizeof(resp->turn_password));
        log_debug("Generated TURN credentials for session %s", session_string);
      } else {
        log_warn("Failed to generate TURN credentials for session %s", session_string);
      }
    }

    log_info("Participant joined session %s (participants=%d/%d, server=%s:%d, type=%s)", session_string,
             session->current_participants, session->max_participants, resp->server_address, resp->server_port,
             session->session_type == SESSION_TYPE_WEBRTC ? "WebRTC" : "DirectTCP");
  } else {
    log_info("Participant joined session %s (participants=%d/%d, IP WITHHELD - auth required)", session_string,
             session->current_participants, session->max_participants);
  }

  mutex_unlock(&session->participant_mutex);
  rwlock_rdunlock(&shard->lock);

  return ASCIICHAT_OK;
}

asciichat_error_t session_leave(session_registry_t *registry, const uint8_t session_id[16],
                                const uint8_t participant_id[16]) {
  if (!registry || !session_id || !participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, session_id, or participant_id is NULL");
  }

  // Need to find session by ID - must check all shards
  // First, acquire read locks on all shards to find the session
  session_entry_t *session = NULL;
  uint32_t found_shard_idx = 0;

  // Acquire read locks on all shards
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    rwlock_rdlock(&registry->shards[i].lock);
  }

  // Find the session
  session = find_session_by_id_unlocked(registry, session_id, &found_shard_idx);

  if (!session) {
    // Release all read locks
    for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
      rwlock_rdunlock(&registry->shards[i].lock);
    }
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found");
  }

  // Acquire per-entry mutex for participant modifications
  mutex_lock(&session->participant_mutex);

  // Find and remove participant
  participant_t *participant = find_participant_locked(session, participant_id);
  if (!participant) {
    mutex_unlock(&session->participant_mutex);
    for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
      rwlock_rdunlock(&registry->shards[i].lock);
    }
    return SET_ERRNO(ERROR_INVALID_STATE, "Participant not in session");
  }

  // Remove participant
  for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
    if (session->participants[i] == participant) {
      SAFE_FREE(session->participants[i]);
      session->current_participants--;
      break;
    }
  }

  char session_string_copy[48];
  SAFE_STRNCPY(session_string_copy, session->session_string, sizeof(session_string_copy));

  log_info("Participant left session %s (participants=%d/%d)", session_string_copy, session->current_participants,
           session->max_participants);

  // If no participants left, mark session for deletion
  bool should_delete = (session->current_participants == 0);

  mutex_unlock(&session->participant_mutex);

  // Release read locks on all shards except the found one
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    if (i != found_shard_idx) {
      rwlock_rdunlock(&registry->shards[i].lock);
    }
  }

  if (should_delete) {
    // Upgrade to write lock on the found shard
    rwlock_rdunlock(&registry->shards[found_shard_idx].lock);
    rwlock_wrlock(&registry->shards[found_shard_idx].lock);

    // Re-find session (it may have been modified while we didn't hold the lock)
    session_entry_t *check_session = NULL;
    HASH_FIND_STR(registry->shards[found_shard_idx].sessions, session_string_copy, check_session);

    if (check_session && check_session->current_participants == 0) {
      log_info("Session %s has no participants, deleting", session_string_copy);
      HASH_DEL(registry->shards[found_shard_idx].sessions, check_session);
      session_entry_free(check_session);
    }

    rwlock_wrunlock(&registry->shards[found_shard_idx].lock);
  } else {
    rwlock_rdunlock(&registry->shards[found_shard_idx].lock);
  }

  return ASCIICHAT_OK;
}

void session_cleanup_expired(session_registry_t *registry) {
  if (!registry) {
    return;
  }

  uint64_t now = get_current_time_ms();
  size_t removed_count = 0;

  // Process each shard independently
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    // Acquire write lock for this shard
    rwlock_wrlock(&registry->shards[i].lock);

    session_entry_t *entry, *tmp;
    HASH_ITER(hh, registry->shards[i].sessions, entry, tmp) {
      if (now > entry->expires_at) {
        log_info("Session %s expired (created_at=%llu, expires_at=%llu, now=%llu)", entry->session_string,
                 (unsigned long long)entry->created_at, (unsigned long long)entry->expires_at, (unsigned long long)now);

        HASH_DEL(registry->shards[i].sessions, entry);
        session_entry_free(entry);
        removed_count++;
      }
    }

    rwlock_wrunlock(&registry->shards[i].lock);
  }

  if (removed_count > 0) {
    log_info("Cleaned up %zu expired sessions", removed_count);
  }
}

// ============================================================================
// Session Lookup by ID (for external use)
// ============================================================================

/**
 * @brief Find session by session_id and copy data to output
 *
 * This is a convenience function for external code (server.c, database.c)
 * that needs to lookup sessions by ID rather than session_string.
 *
 * @param registry Session registry
 * @param session_id Session UUID
 * @param out_session Output: copy of session data (caller must not free)
 * @return Pointer to session entry (valid only while holding locks), or NULL
 */
session_entry_t *session_find_by_id(session_registry_t *registry, const uint8_t session_id[16]) {
  if (!registry || !session_id) {
    return NULL;
  }

  // Acquire read locks on all shards
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    rwlock_rdlock(&registry->shards[i].lock);
  }

  session_entry_t *session = find_session_by_id_unlocked(registry, session_id, NULL);

  // Release all read locks
  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    rwlock_rdunlock(&registry->shards[i].lock);
  }

  return session;
}

/**
 * @brief Find session by session_string
 *
 * Thread-safe lookup that acquires/releases the appropriate shard lock.
 *
 * @param registry Session registry
 * @param session_string Session string to find
 * @return Pointer to session entry (valid only while holding locks), or NULL
 */
session_entry_t *session_find_by_string(session_registry_t *registry, const char *session_string) {
  if (!registry || !session_string) {
    return NULL;
  }

  uint32_t shard_idx = get_shard_index(session_string);
  session_shard_t *shard = &registry->shards[shard_idx];

  rwlock_rdlock(&shard->lock);

  session_entry_t *session = NULL;
  HASH_FIND_STR(shard->sessions, session_string, session);

  rwlock_rdunlock(&shard->lock);

  return session;
}

/**
 * @brief Iterate over all sessions (for database operations)
 *
 * Calls the callback for each session while holding the appropriate shard lock.
 * The callback should NOT store the session pointer - it's only valid during the callback.
 *
 * @param registry Session registry
 * @param callback Function to call for each session
 * @param user_data User data passed to callback
 */
void session_foreach(session_registry_t *registry, void (*callback)(session_entry_t *session, void *user_data),
                     void *user_data) {
  if (!registry || !callback) {
    return;
  }

  for (uint32_t i = 0; i < SESSION_REGISTRY_NUM_SHARDS; i++) {
    rwlock_rdlock(&registry->shards[i].lock);

    session_entry_t *entry, *tmp;
    HASH_ITER(hh, registry->shards[i].sessions, entry, tmp) {
      callback(entry, user_data);
    }

    rwlock_rdunlock(&registry->shards[i].lock);
  }
}

/**
 * @brief Add a session entry directly to the registry (for database loading)
 *
 * This bypasses the normal creation flow and adds a pre-populated entry.
 * Used by database_load_sessions() to restore sessions from disk.
 *
 * @param registry Session registry
 * @param session Pre-allocated and populated session entry (ownership transferred)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
asciichat_error_t session_add_entry(session_registry_t *registry, session_entry_t *session) {
  if (!registry || !session) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry or session is NULL");
  }

  uint32_t shard_idx = get_shard_index(session->session_string);
  session_shard_t *shard = &registry->shards[shard_idx];

  rwlock_wrlock(&shard->lock);

  // Check for duplicate
  session_entry_t *existing = NULL;
  HASH_FIND_STR(shard->sessions, session->session_string, existing);
  if (existing) {
    rwlock_wrunlock(&shard->lock);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session already exists: %s", session->session_string);
  }

  HASH_ADD_STR(shard->sessions, session_string, session);

  rwlock_wrunlock(&shard->lock);

  return ASCIICHAT_OK;
}
