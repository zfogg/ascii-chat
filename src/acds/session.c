/**
 * @file acds/session.c
 * @brief 🎯 Session registry implementation (lock-free RCU)
 *
 * High-performance session management using liburcu (Read-Copy-Update):
 *   - Lock-free read-side operations (no locks acquired on lookups)
 *   - RCU hash table (cds_lfht) replacing uthash + rwlock
 *   - Fine-grained per-entry locking for participant modifications
 *   - Deferred memory freeing via call_rcu() for thread safety
 *
 * Sessions are ephemeral (24-hour expiration) and stored in memory.
 * Expected 5-10x performance improvement under high concurrency.
 */

#include "acds/session.h"
#include "acds/main.h"
#include "acds/strings.h"
#include "log/logging.h"
#include "network/webrtc/turn_credentials.h"
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
// RCU Hash Table Helpers
// ============================================================================

/**
 * @brief Hash function for session string lookup (DJB2 algorithm)
 * @param session_string Session string (null-terminated)
 * @return Hash value for RCU hash table
 */
static unsigned long session_string_hash(const char *session_string) {
  unsigned long hash = 5381;
  int c;
  while ((c = (unsigned char)*session_string++)) {
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  }
  return hash;
}

/**
 * @brief Match function for RCU hash table lookups
 * @param node Hash table node (session_entry_t)
 * @param key Session string from lookup request
 * @return 0 if match, non-zero otherwise
 */
static int session_string_match(struct cds_lfht_node *node, const void *key) {
  const session_entry_t *entry = caa_container_of(node, session_entry_t, hash_node);
  const char *session_string = (const char *)key;
  return strcmp(entry->session_string, session_string) != 0 ? 1 : 0;
}

/**
 * @brief RCU callback for deferred session freeing
 * Called after RCU grace period expires (no more readers)
 * @param head RCU head pointer from call_rcu()
 */
static void session_free_rcu(struct rcu_head *head) {
  session_entry_t *entry = caa_container_of(head, session_entry_t, rcu_head);

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

/**
 * @brief Find session by session_id (RCU read-side)
 * Caller must be in RCU read-side critical section via rcu_read_lock()
 *
 * @param registry Session registry
 * @param session_id Session UUID
 * @return Session entry or NULL if not found
 */
static session_entry_t *find_session_by_id_rcu(session_registry_t *registry, const uint8_t session_id[16]) {
  session_entry_t *entry;
  struct cds_lfht_iter iter;

  /* Iterate through all entries in RCU hash table */
  cds_lfht_for_each_entry(registry->sessions, &iter, entry, hash_node) {
    if (memcmp(entry->session_id, session_id, 16) == 0) {
      return entry;
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

// ============================================================================
// Registry Lifecycle
// ============================================================================

asciichat_error_t session_registry_init(session_registry_t *registry) {
  if (!registry) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry is NULL");
  }

  memset(registry, 0, sizeof(*registry));

  /* Create RCU lock-free hash table
     - Initial size: 256 buckets
     - Auto-resize enabled
     - No flags needed for simplicity
   */
  registry->sessions = cds_lfht_new(256, 256, 0, CDS_LFHT_AUTO_RESIZE | CDS_LFHT_ACCOUNTING, NULL);
  if (!registry->sessions) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to create RCU hash table");
  }

  log_info("Session registry initialized (RCU lock-free hash table)");
  return ASCIICHAT_OK;
}

void session_registry_destroy(session_registry_t *registry) {
  if (!registry || !registry->sessions) {
    return;
  }

  /* RCU read-side critical section to safely iterate */
  rcu_read_lock();

  session_entry_t *entry;
  struct cds_lfht_iter iter;

  /* Collect entries to delete */
  struct cds_list_head *to_delete = cds_list_head_new();
  if (!to_delete) {
    rcu_read_unlock();
    log_error("Failed to allocate list for cleanup");
    return;
  }

  /* Iterate and collect entries (can't delete during iteration) */
  cds_lfht_for_each_entry(registry->sessions, &iter, entry, hash_node) {
    cds_list_add_tail(&entry->hash_node.next, to_delete); /* Reuse for list linkage */
  }

  rcu_read_unlock();

  /* Now delete entries outside RCU critical section */
  /* In a real scenario, we'd schedule call_rcu() for each entry */
  /* For now, just free immediately since we're destroying the whole registry */
  cds_lfht_for_each_entry(registry->sessions, &iter, entry, hash_node) {
    cds_lfht_del(registry->sessions, &entry->hash_node);
    session_free_rcu(&entry->rcu_head);
  }

  SAFE_FREE(to_delete);

  /* Destroy the RCU hash table */
  int ret = cds_lfht_destroy(registry->sessions, NULL);
  if (ret < 0) {
    log_warn("Failed to destroy RCU hash table (still has entries)");
  }

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

  // Initialize RCU node
  cds_lfht_node_init(&session->hash_node);

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

  /* Insert into RCU hash table
     Returns duplicate if already exists
   */
  unsigned long hash = session_string_hash(session_string);
  struct cds_lfht_node *ret_node =
      cds_lfht_add_unique(registry->sessions, hash, session_string_match, session_string, &session->hash_node);

  if (ret_node != &session->hash_node) {
    // Duplicate exists - cleanup and return error
    mutex_destroy(&session->participant_mutex);
    SAFE_FREE(session);
    return SET_ERRNO(ERROR_INVALID_STATE, "Session string already exists: %s", session_string);
  }

  // Fill response
  resp->session_string_len = (uint8_t)strlen(session_string);
  SAFE_STRNCPY(resp->session_string, session_string, sizeof(resp->session_string));
  memcpy(resp->session_id, session->session_id, 16);
  resp->expires_at = session->expires_at;

  // Populate STUN/TURN server counts from config
  resp->stun_count = config->stun_count;
  resp->turn_count = config->turn_count;

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

  /* RCU read-side critical section - NO LOCK ACQUIRED!
     This is the key performance improvement: lookups never contend
   */
  rcu_read_lock();

  // Find session by string using RCU hash table
  unsigned long hash = session_string_hash(session_string);
  struct cds_lfht_iter iter;
  cds_lfht_lookup(registry->sessions, hash, session_string_match, session_string, &iter);
  struct cds_lfht_node *node = cds_lfht_iter_get_node(&iter);

  if (!node) {
    rcu_read_unlock();
    resp->found = 0;
    log_debug("Session lookup failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  session_entry_t *session = caa_container_of(node, session_entry_t, hash_node);

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

  rcu_read_unlock();

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

  /* RCU read-side critical section for lookup
     (Read-side critical section, not write lock!)
   */
  rcu_read_lock();

  // Find session
  unsigned long hash = session_string_hash(session_string);
  struct cds_lfht_iter iter;
  cds_lfht_lookup(registry->sessions, hash, session_string_match, session_string, &iter);
  struct cds_lfht_node *node = cds_lfht_iter_get_node(&iter);

  if (!node) {
    rcu_read_unlock();
    resp->error_code = ACIP_ERROR_SESSION_NOT_FOUND;
    SAFE_STRNCPY(resp->error_message, "Session not found", sizeof(resp->error_message));
    log_warn("Session join failed: %s (not found)", session_string);
    return ASCIICHAT_OK;
  }

  session_entry_t *session = caa_container_of(node, session_entry_t, hash_node);

  // Acquire fine-grained per-entry mutex for participant modifications
  mutex_lock(&session->participant_mutex);

  // Check if session full
  if (session->current_participants >= session->max_participants) {
    mutex_unlock(&session->participant_mutex);
    rcu_read_unlock();
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "Session is full", sizeof(resp->error_message));
    log_warn("Session join failed: %s (full)", session_string);
    return ASCIICHAT_OK;
  }

  // Verify password if required
  if (session->has_password && req->has_password) {
    if (!verify_password(req->password, session->password_hash)) {
      mutex_unlock(&session->participant_mutex);
      rcu_read_unlock();
      resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
      SAFE_STRNCPY(resp->error_message, "Invalid password", sizeof(resp->error_message));
      log_warn("Session join failed: %s (invalid password)", session_string);
      return ASCIICHAT_OK;
    }
  } else if (session->has_password && !req->has_password) {
    mutex_unlock(&session->participant_mutex);
    rcu_read_unlock();
    resp->error_code = ACIP_ERROR_INVALID_PASSWORD;
    SAFE_STRNCPY(resp->error_message, "Password required", sizeof(resp->error_message));
    log_warn("Session join failed: %s (password required)", session_string);
    return ASCIICHAT_OK;
  }

  // Find empty participant slot
  int slot = find_empty_slot_locked(session);
  if (slot < 0) {
    mutex_unlock(&session->participant_mutex);
    rcu_read_unlock();
    resp->error_code = ACIP_ERROR_SESSION_FULL;
    SAFE_STRNCPY(resp->error_message, "No participant slots available", sizeof(resp->error_message));
    log_error("Session join failed: %s (no slots, but count was not full)", session_string);
    return ASCIICHAT_OK;
  }

  // Allocate participant
  participant_t *participant = SAFE_MALLOC(sizeof(participant_t), participant_t *);
  if (!participant) {
    mutex_unlock(&session->participant_mutex);
    rcu_read_unlock();
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
  rcu_read_unlock();

  return ASCIICHAT_OK;
}

asciichat_error_t session_leave(session_registry_t *registry, const uint8_t session_id[16],
                                const uint8_t participant_id[16]) {
  if (!registry || !session_id || !participant_id) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "registry, session_id, or participant_id is NULL");
  }

  rcu_read_lock();

  // Find session by ID
  session_entry_t *session = find_session_by_id_rcu(registry, session_id);
  if (!session) {
    rcu_read_unlock();
    return SET_ERRNO(ERROR_INVALID_STATE, "Session not found");
  }

  // Acquire per-entry mutex for participant modifications
  mutex_lock(&session->participant_mutex);

  // Find and remove participant
  participant_t *participant = find_participant_locked(session, participant_id);
  if (!participant) {
    mutex_unlock(&session->participant_mutex);
    rcu_read_unlock();
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

  // If no participants left, mark session for deletion
  bool should_delete = (session->current_participants == 0);

  mutex_unlock(&session->participant_mutex);

  if (should_delete) {
    log_info("Session %s has no participants, deleting", session->session_string);

    // Delete from RCU hash table and schedule deferred freeing
    cds_lfht_del(registry->sessions, &session->hash_node);
    call_rcu(&session->rcu_head, session_free_rcu);
  }

  rcu_read_unlock();

  return ASCIICHAT_OK;
}

void session_cleanup_expired(session_registry_t *registry) {
  if (!registry || !registry->sessions) {
    return;
  }

  uint64_t now = get_current_time_ms();
  size_t removed_count = 0;

  rcu_read_lock();

  session_entry_t *entry;
  struct cds_lfht_iter iter;

  // Iterate through hash table and collect expired sessions
  cds_lfht_for_each_entry(registry->sessions, &iter, entry, hash_node) {
    if (now > entry->expires_at) {
      log_info("Session %s expired (created_at=%llu, expires_at=%llu, now=%llu)", entry->session_string,
               (unsigned long long)entry->created_at, (unsigned long long)entry->expires_at, (unsigned long long)now);

      // Delete from hash table
      cds_lfht_del(registry->sessions, &entry->hash_node);

      // Schedule deferred freeing via RCU callback
      call_rcu(&entry->rcu_head, session_free_rcu);
      removed_count++;
    }
  }

  rcu_read_unlock();

  if (removed_count > 0) {
    log_info("Cleaned up %zu expired sessions", removed_count);

    // Optionally synchronize RCU if many deletions to avoid callback backlog
    if (removed_count > 100) {
      log_debug("Synchronizing RCU after bulk session cleanup");
      synchronize_rcu();
    }
  }
}
