-- name: CreateSession
-- access: write
-- rows: zero
INSERT INTO sessions (
  session_string,
  session_id,
  host_pubkey,
  password_hash,
  max_participants,
  current_participants,
  capabilities,
  has_password,
  expose_ip_publicly,
  session_type,
  server_address,
  server_port,
  created_at,
  expires_at,
  last_activity_at
)
VALUES (
  :session_string,
  :session_id,
  :host_pubkey,
  :password_hash,
  :max_participants,
  0,
  :capabilities,
  :has_password,
  :expose_ip_publicly,
  :session_type,
  :server_address,
  :server_port,
  :created_at,
  :expires_at,
  :last_activity_at
)

-- name: LookupSession
-- access: read
-- rows: zero-or-one
SELECT
  session_string,
  session_id,
  host_pubkey,
  password_hash,
  max_participants,
  current_participants,
  capabilities,
  has_password,
  expose_ip_publicly,
  session_type,
  server_address,
  server_port,
  created_at,
  expires_at,
  last_activity_at
FROM sessions
WHERE
  session_string = :session_string

-- name: InsertParticipant
-- access: write
-- rows: zero
INSERT INTO participants (
  participant_id,
  session_string,
  identity_pubkey,
  joined_at
)
VALUES (
  :participant_id,
  :session_string,
  :identity_pubkey,
  :joined_at
)

-- name: IncrementParticipants
-- access: write
-- rows: zero
UPDATE sessions
SET
  current_participants = current_participants + 1,
  last_activity_at = :last_activity_at
WHERE
  session_string = :session_string

-- name: SetSessionInitiator
-- access: write
-- rows: zero
UPDATE sessions
SET
  initiator_id = :initiator_id
WHERE
  session_string = :session_string

-- name: LookupSessionString
-- access: read
-- rows: zero-or-one
SELECT
  session_string
FROM sessions
WHERE
  session_id = :session_id

-- name: DeleteParticipant
-- access: write
-- rows: zero
DELETE FROM participants
WHERE
  participant_id = :participant_id
  AND session_string = :session_string

-- name: DecrementParticipants
-- access: write
-- rows: zero
UPDATE sessions
SET
  current_participants = current_participants - 1,
  last_activity_at = :last_activity_at
WHERE
  session_string = :session_string
  AND current_participants > 0

-- name: GetParticipantCount
-- access: read
-- rows: zero-or-one
SELECT
  current_participants
FROM sessions
WHERE
  session_string = :session_string

-- name: DeleteSession
-- access: write
-- rows: zero
DELETE FROM sessions
WHERE
  session_string = :session_string

-- name: FindExpiredSessions
-- access: read
-- rows: many
SELECT
  session_string,
  last_activity_at
FROM sessions
WHERE
  last_activity_at < :cutoff_time

-- name: DeleteExpiredSessions
-- access: write
-- rows: zero
DELETE FROM sessions
WHERE
  last_activity_at < :cutoff_time

-- name: UpdateHost
-- access: write
-- rows: zero
UPDATE sessions
SET
  host_established = 1,
  in_migration = 0,
  migration_start_ns = 0,
  host_participant_id = :host_participant_id,
  host_address = :host_address,
  host_port = :host_port,
  host_connection_type = :host_connection_type,
  last_activity_at = :last_activity_at
WHERE
  session_id = :session_id

-- name: ClearHost
-- access: write
-- rows: zero
UPDATE sessions
SET
  host_established = 0,
  host_participant_id = NULL,
  host_address = NULL,
  host_port = 0,
  host_connection_type = 0
WHERE
  session_id = :session_id

-- name: StartMigration
-- access: write
-- rows: zero
UPDATE sessions
SET
  in_migration = 1,
  migration_start_ns = :migration_start_ns
WHERE
  session_id = :session_id

-- name: GetMigrationState
-- access: read
-- rows: zero-or-one
SELECT
  in_migration,
  migration_start_ns
FROM sessions
WHERE
  session_id = :session_id

-- name: AddSessionKey
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_identity_keys (
  session_string,
  key_type,
  identity_pubkey,
  key_version,
  added_at
)
VALUES (
  :session_string,
  'ed25519',
  :identity_pubkey,
  :key_version,
  :added_at
)

-- name: RevokeSessionKey
-- access: write
-- rows: zero
UPDATE session_identity_keys
SET
  revoked = 1
WHERE
  session_string = :session_string
  AND key_type = 'ed25519' AND identity_pubkey = :identity_pubkey

-- name: VerifySessionKey
-- access: read
-- rows: zero-or-one
SELECT
  1
FROM session_identity_keys
WHERE
  session_string = :session_string
  AND key_type = 'ed25519' AND identity_pubkey = :identity_pubkey
  AND revoked = 0

-- name: GetSessionKeys
-- access: read
-- rows: many
SELECT
  identity_pubkey
FROM session_identity_keys
WHERE
  session_string = :session_string
  AND key_type = 'ed25519' AND revoked = 0
ORDER BY
  key_version ASC, identity_pubkey ASC

-- name: GetStatsSnapshot
-- access: read
-- rows: many
SELECT
  session_string,
  current_participants,
  host_established,
  in_migration
FROM sessions
WHERE
  expires_at > :now
ORDER BY
  session_string

-- name: GetSessionParticipants
-- access: read
-- rows: many
SELECT
  participant_id,
  identity_pubkey,
  joined_at
FROM participants
WHERE
  session_string = :session_string

-- name: GetSessionById
-- access: read
-- rows: zero-or-one
SELECT
  session_string,
  session_id,
  host_pubkey,
  password_hash,
  max_participants,
  current_participants,
  capabilities,
  has_password,
  expose_ip_publicly,
  session_type,
  server_address,
  server_port,
  created_at,
  expires_at,
  last_activity_at,
  initiator_id,
  host_established,
  host_participant_id,
  host_address,
  host_port,
  host_connection_type,
  in_migration,
  migration_start_ns
FROM sessions WHERE session_id = :session_id

-- name: GetSessionByString
-- access: read
-- rows: zero-or-one
SELECT
  session_string,
  session_id,
  host_pubkey,
  password_hash,
  max_participants,
  current_participants,
  capabilities,
  has_password,
  expose_ip_publicly,
  session_type,
  server_address,
  server_port,
  created_at,
  expires_at,
  last_activity_at,
  initiator_id,
  host_established,
  host_participant_id,
  host_address,
  host_port,
  host_connection_type,
  in_migration,
  migration_start_ns
FROM sessions WHERE session_string = :session_string

-- name: CountSessions
-- access: read
-- rows: one
SELECT
  COUNT(*)
FROM sessions
WHERE
  expires_at > :now

-- name: CountUniqueClients
-- access: read
-- rows: one
SELECT
  COUNT(DISTINCT participant_id)
FROM participants

-- name: BeginTransaction
-- access: write
-- rows: zero
BEGIN IMMEDIATE;

-- name: CommitTransaction
-- access: write
-- rows: zero
COMMIT;

-- name: RollbackTransaction
-- access: write
-- rows: zero
ROLLBACK;

-- name: EnableWAL
-- access: write
-- rows: one
PRAGMA journal_mode = WAL;

-- name: EnableForeignKeys
-- access: write
-- rows: zero
PRAGMA foreign_keys = ON;

-- name: BackfillSessionIdentityKeys
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_identity_keys
SELECT
  s.session_string,
  'ed25519',
  s.host_pubkey,
  0,
  s.created_at,
  0
FROM sessions s
WHERE typeof(s.host_pubkey) = 'blob'
  AND length(s.host_pubkey) = 32
  AND s.host_pubkey != zeroblob(32)
  AND NOT EXISTS (SELECT 1 FROM session_key_policy p WHERE p.session_string = s.session_string);

-- name: BackfillSessionKeyPolicy
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_key_policy
SELECT
  session_string,
  0
FROM sessions;

-- name: ImportLegacySessionKeys
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_identity_keys
SELECT
  k.session_string,
  'ed25519',
  k.identity_pubkey,
  k.key_version,
  k.added_at,
  CASE
    WHEN k.revoked = 0 THEN 0
    ELSE 1
  END
FROM session_keys k
WHERE typeof(k.identity_pubkey) = 'blob'
  AND length(k.identity_pubkey) = 32
  AND EXISTS (SELECT 1 FROM sessions s WHERE s.session_string = k.session_string)
  AND NOT EXISTS (SELECT 1 FROM session_key_policy p WHERE p.session_string = k.session_string);

-- name: HasLegacySessionKeys
-- access: read
-- rows: zero-or-one
SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'session_keys'

-- name: InsertSessionKeyPolicy
-- access: write
-- rows: zero
INSERT INTO session_key_policy VALUES (:session_string, :restricted)

-- name: GetSessionKeyPolicy
-- access: read
-- rows: zero-or-one
SELECT restricted FROM session_key_policy WHERE session_string = :session_string

-- name: BeginKeySet
-- access: write
-- rows: zero
SAVEPOINT create_key_set

-- name: CommitKeySet
-- access: write
-- rows: zero
RELEASE create_key_set

-- name: RollbackToKeySet
-- access: write
-- rows: zero
ROLLBACK TO create_key_set;

-- name: BeginHostTransportRefresh
-- access: write
-- rows: zero
SAVEPOINT host_transport_refresh;

-- name: CommitHostTransportRefresh
-- access: write
-- rows: zero
RELEASE host_transport_refresh;

-- name: RollbackHostTransportRefresh
-- access: write
-- rows: zero
ROLLBACK TO host_transport_refresh;

-- name: SetSessionTransports
-- access: write
-- rows: zero
INSERT OR REPLACE INTO session_transports(session_id, metadata) VALUES (:session_id, :metadata);

-- name: SetSessionTransportHost
-- access: write
-- rows: zero
INSERT OR REPLACE INTO session_transport_hosts(session_id, host_id) VALUES (:session_id, :host_id);

-- name: GetSessionTransports
-- access: read
-- rows: zero-or-one
SELECT t.metadata
FROM session_transports t
JOIN sessions s ON s.session_id = t.session_id
LEFT JOIN session_transport_hosts h ON h.session_id = t.session_id
WHERE t.session_id = :session_id
  AND s.in_migration = 0
  AND COALESCE(s.host_participant_id, s.initiator_id) IS COALESCE(h.host_id, s.initiator_id);
