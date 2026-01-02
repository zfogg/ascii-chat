/**
 * @file acds/session.c
 * @brief 🎯 Session registry implementation
 *
 * Thread-safe session management using uthash and rwlock.
 * Sessions are ephemeral (24-hour expiration) and stored in memory.
 */

#include "acds/session.h"
#include "acds/main.h"
#include "acds/strings.h"
#include "log/logging.h"
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
 * @brief Hash password with Argon2id
 * @param password Cleartext password
 * @param hash_out Output buffer for hash (128 bytes)
 * @return ASCIICHAT_OK on success, error code otherwise
 */
static asciichat_error_t hash_password(const char *password, char hash_out[128]) {
  // Use libsodium's crypto_pwhash_str for Argon2id hashing
  // Output is ASCII string (null-terminated)
  if (crypto_pwhash_str((char *)hash_out, password, strlen(password), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                        crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
    return SET_ERRNO(ERROR_GENERAL, "Failed to hash password (out of memory)");
  }

  return ASCIICHAT_OK;
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

/**
 * @brief Find session by session_id (caller must hold read/write lock)
 * @param registry Session registry
 * @param session_id Session UUID
 * @return Session entry or NULL if not found
 */
static session_entry_t *find_session_by_id_locked(session_registry_t *registry, const uint8_t session_id[16]) {
  session_entry_t *entry, *tmp;
  HASH_ITER(hh, registry->sessions, entry, tmp) {
    if (memcmp(entry->session_id, session_id, 16) == 0) {
      return entry;
    }
  }
  return NULL;
}

/**
 * @brief Find participant in session (caller must hold lock)
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
 * @brief Find empty participant slot in session (caller must hold lock)
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

// ============================================================================
// Registry Lifecycle
// ============================================================================

asciichat_error_t session_registry_init(session_registry_t *registry) {
  if (!registry) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry is NULL");
  }

  memset(registry, 0, sizeof(*registry));
  registry->sessions = NULL; // uthash initializes to NULL

  asciichat_error_t result = rwlock_init(&registry->lock);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to initialize rwlock");
  }

  log_info("Session registry initialized");
  return ASCIICHAT_OK;
}

void session_registry_destroy(session_registry_t *registry) {
  if (!registry) {
    return;
  }

  rwlock_wrlock(&registry->lock);

  // Free all sessions
  session_entry_t *entry, *tmp;
  HASH_ITER(hh, registry->sessions, entry, tmp) {
    // Free participants
    for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
      if (entry->participants[i]) {
        SAFE_FREE(entry->participants[i]);
      }
    }

    HASH_DEL(registry->sessions, entry);
    SAFE_FREE(entry);
  }
  registry->sessions = NULL;

  rwlock_wrunlock(&registry->lock);
  rwlock_destroy(&registry->lock);

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

  // Acquire write lock
  rwlock_wrlock(&registry->lock);

  // Check if session string already exists
  session_entry_t *existing = NULL;
  HASH_FIND_STR(registry->sessions, session_string, existing);
  if (existing) {
    rwlock_wrunlock(&registry->lock);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session string already exists: %s", session_string);
  }

  // Allocate new session entry
  session_entry_t *session = SAFE_MALLOC(sizeof(session_entry_t), session_entry_t *);
  if (!session) {
    rwlock_wrunlock(&registry->lock);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate session entry");
  }

  memset(session, 0, sizeof(*session));

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

  // Set timestamps
  uint64_t now = get_current_time_ms();
  session->created_at = now;
  session->expires_at = now + ACIP_SESSION_EXPIRATION_MS;

  // Add to hash table
  HASH_ADD_STR(registry->sessions, session_string, session);

  // Fill response
  resp->session_string_len = (uint8_t)strlen(session_string);
  SAFE_STRNCPY(resp->session_string, session_string, sizeof(resp->session_string));
  memcpy(resp->session_id, session->session_id, 16);
  resp->expires_at = session->expires_at;

  // Populate STUN/TURN server counts from config
  resp->stun_count = config->stun_count;
  resp->turn_count = config->turn_count;

  rwlock_wrunlock(&registry->lock);

  log_info("Session created: %s (max_participants=%d, has_password=%d)", session_string, session->max_participants,
           session->has_password);

  return ASCIICHAT_OK;
}

asciichat_error_t session_lookup(session_registry_t *registry, const char *session_string, const acds_config_t *config,
                                 acip_session_info_t *resp) {
  if (!registry || !session_string || !config || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, session_string, config, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));

  // Acquire read lock
  rwlock_rdlock(&registry->lock);

  // Find session by string
  session_entry_t *session = NULL;
  HASH_FIND_STR(registry->sessions, session_string, session);

  if (!session) {
    rwlock_rdunlock(&registry->lock);
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

  // NOTE: Server connection information (IP/port) is NOT included in SESSION_INFO.
  // It is only revealed after successful authentication via SESSION_JOIN to prevent
  // IP address leakage to unauthenticated clients.

  rwlock_rdunlock(&registry->lock);

  log_debug("Session lookup: %s (found, participants=%d/%d)", session_string, resp->current_participants,
            resp->max_participants);

  return ASCIICHAT_OK;
}

asciichat_error_t session_join(session_registry_t *registry, const acip_session_join_t *req,
                               acip_session_joined_t *resp) {
  if (!registry || !req || !resp) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, req, or resp is NULL");
  }

  memset(resp, 0, sizeof(*resp));
  resp->success = 0;

  // Extract session string (null-terminate)
  char session_string[ACIP_MAX_SESSION_STRING_LEN] = {0};
  size_t len = req->session_string_len < (ACIP_MAX_SESSION_STRING_LEN - 1) ? req->session_string_len
                                                                           : (ACIP_MAX_SESSION_STRING_LEN - 1);
  memcpy(session_string, req->session_string, len);
  session_string[len] = '\0';

  // Acquire write lock (we might add participant)
  rwlock_wrlock(&registry->lock);

  // Find session
  session_entry_t *session = NULL;
  HASH_FIND_STR(registry->sessions, session_string, session);

  if (!session) {
    rwlock_wrunlock(&registry->lock);
    resp->error_code = ACIP_ERROR_SESSION_NOT_FOUND;
    SAFE_STRNCPY(resp->error_message, "Session not found", sizeof(resp->error_message));
    log_warn("Session join failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  // Check if session full
  if (session->current_participants >= session->max_participants) {
    rwlock_wrunlock(&registry->lock);
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "Session is full", sizeof(resp->error_message));
    log_warn("Session join failed: %s (full)", session_string);
    return ASCIICHAT_OK;
  }

  // Verify password if required
  if (session->has_password && req->has_password) {
    if (!verify_password(req->password, session->password_hash)) {
      rwlock_wrunlock(&registry->lock);
      resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
      SAFE_STRNCPY(resp->error_message, "Invalid password", sizeof(resp->error_message));
      log_warn("Session join failed: %s (invalid password)", session_string);
      return ASCIICHAT_OK;
    }
  } else if (session->has_password && !req->has_password) {
    rwlock_wrunlock(&registry->lock);
    resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
    SAFE_STRNCPY(resp->error_message, "Password required", sizeof(resp->error_message));
    log_warn("Session join failed: %s (password required)", session_string);
    return ASCIICHAT_OK;
  }

  // Find empty participant slot
  int slot = find_empty_slot_locked(session);
  if (slot < 0) {
    rwlock_wrunlock(&registry->lock);
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "No participant slots available", sizeof(resp->error_message));
    log_error("Session join failed: %s (no slots, but count was not full)", session_string);
    return ASCIICHAT_OK;
  }

  // Allocate participant
  participant_t *participant = SAFE_MALLOC(sizeof(participant_t), participant_t *);
  if (!participant) {
    rwlock_wrunlock(&registry->lock);
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
  //
  // IP address is ONLY revealed if:
  // 1. Password was verified (if session has_password), OR
  // 2. Session explicitly opted-in with expose_ip_publicly flag
  //
  // This prevents IP address leakage to unauthenticated clients.
  bool reveal_ip = false;

  if (session->has_password) {
    // Password was already verified at line 355-370
    // If we reached here, password is correct
    reveal_ip = true;
  } else if (session->expose_ip_publicly) {
    // No password, but explicit opt-in via --acds-expose-ip
    reveal_ip = true;
  } else {
    // No password AND no explicit opt-in = SECURITY VIOLATION
    // This session was created without proper privacy controls
    log_warn("Session join: %s has no password and expose_ip_publicly=false - IP NOT REVEALED", session_string);
    reveal_ip = false;
  }

  if (reveal_ip) {
    SAFE_STRNCPY(resp->server_address, session->server_address, sizeof(resp->server_address));
    resp->server_port = session->server_port;
    log_info("Participant joined session %s (participants=%d/%d, server=%s:%d)", session_string,
             session->current_participants, session->max_participants, resp->server_address, resp->server_port);
  } else {
    // Leave server_address and server_port as zero (memset at line 321)
    log_info("Participant joined session %s (participants=%d/%d, IP WITHHELD - auth required)", session_string,
             session->current_participants, session->max_participants);
  }

  rwlock_wrunlock(&registry->lock);

  return ASCIICHAT_OK;
}

asciichat_error_t session_leave(session_registry_t *registry, const uint8_t session_id[16],
                                const uint8_t participant_id[16]) {
  if (!registry || !session_id || !participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, session_id, or participant_id is NULL");
  }

  rwlock_wrlock(&registry->lock);

  // Find session by ID
  session_entry_t *session = find_session_by_id_locked(registry, session_id);
  if (!session) {
    rwlock_wrunlock(&registry->lock);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found");
  }

  // Find and remove participant
  participant_t *participant = find_participant_locked(session, participant_id);
  if (!participant) {
    rwlock_wrunlock(&registry->lock);
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

  log_info("Participant left session %s (participants=%d/%d)", session->session_string, session->current_participants,
           session->max_participants);

  // If no participants left, delete session
  if (session->current_participants == 0) {
    log_info("Session %s has no participants, deleting", session->session_string);
    HASH_DEL(registry->sessions, session);
    SAFE_FREE(session);
  }

  rwlock_wrunlock(&registry->lock);

  return ASCIICHAT_OK;
}

void session_cleanup_expired(session_registry_t *registry) {
  if (!registry) {
    return;
  }

  uint64_t now = get_current_time_ms();
  size_t removed_count = 0;

  rwlock_wrlock(&registry->lock);

  session_entry_t *entry, *tmp;
  HASH_ITER(hh, registry->sessions, entry, tmp) {
    if (now > entry->expires_at) {
      log_info("Session %s expired (created_at=%llu, expires_at=%llu, now=%llu)", entry->session_string,
               (unsigned long long)entry->created_at, (unsigned long long)entry->expires_at, (unsigned long long)now);

      // Free participants
      for (size_t i = 0; i < MAX_PARTICIPANTS; i++) {
        if (entry->participants[i]) {
          SAFE_FREE(entry->participants[i]);
        }
      }

      HASH_DEL(registry->sessions, entry);
      SAFE_FREE(entry);
      removed_count++;
    }
  }

  rwlock_wrunlock(&registry->lock);

  if (removed_count > 0) {
    log_info("Cleaned up %zu expired sessions", removed_count);
  }
}
