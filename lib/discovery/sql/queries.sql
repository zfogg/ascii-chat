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
VALUES
  (?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?, ?, ?)

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
  session_string = ?

-- name: InsertParticipant
-- access: write
-- rows: zero
INSERT INTO participants (participant_id, session_string, identity_pubkey, joined_at)
VALUES
  (?, ?, ?, ?)

-- name: IncrementParticipants
-- access: write
-- rows: zero
UPDATE sessions
SET
  current_participants = current_participants + 1,
  last_activity_at = ?
WHERE
  session_string = ?

-- name: SetSessionInitiator
-- access: write
-- rows: zero
UPDATE sessions
SET
  initiator_id = ?
WHERE
  session_string = ?

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
  participant_id = ?
  AND session_string = ?

-- name: DecrementParticipants
-- access: write
-- rows: zero
UPDATE sessions
SET
  current_participants = current_participants - 1,
  last_activity_at = ?
WHERE
  session_string = ?
  AND current_participants > 0

-- name: GetParticipantCount
-- access: read
-- rows: zero-or-one
SELECT
  current_participants
FROM sessions
WHERE
  session_string = ?

-- name: DeleteSession
-- access: write
-- rows: zero
DELETE FROM sessions
WHERE
  session_string = ?

-- name: FindExpiredSessions
-- access: read
-- rows: many
SELECT
  session_string,
  last_activity_at
FROM sessions
WHERE
  last_activity_at < ?

-- name: DeleteExpiredSessions
-- access: write
-- rows: zero
DELETE FROM sessions
WHERE
  last_activity_at < ?

-- name: UpdateHost
-- access: write
-- rows: zero
UPDATE sessions
SET
  host_established = 1,
  in_migration = 0,
  migration_start_ns = 0,
  host_participant_id = ?,
  host_address = ?,
  host_port = ?,
  host_connection_type = ?,
  last_activity_at = ?
WHERE
  session_id = ?

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
  session_id = ?

-- name: StartMigration
-- access: write
-- rows: zero
UPDATE sessions
SET
  in_migration = 1,
  migration_start_ns = ?
WHERE
  session_id = ?

-- name: GetMigrationState
-- access: read
-- rows: zero-or-one
SELECT
  in_migration,
  migration_start_ns
FROM sessions
WHERE
  session_id = ?

-- name: AddSessionKey
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_identity_keys (session_string, key_type, identity_pubkey, key_version, added_at)
VALUES
  (?, 'ed25519', ?, ?, ?)

-- name: RevokeSessionKey
-- access: write
-- rows: zero
UPDATE session_identity_keys
SET
  revoked = 1
WHERE
  session_string = ?
  AND key_type = 'ed25519' AND identity_pubkey = ?

-- name: VerifySessionKey
-- access: read
-- rows: zero-or-one
SELECT
  1
FROM session_identity_keys
WHERE
  session_string = ?
  AND key_type = 'ed25519' AND identity_pubkey = ?
  AND revoked = 0

-- name: GetSessionKeys
-- access: read
-- rows: many
SELECT
  identity_pubkey
FROM session_identity_keys
WHERE
  session_string = ?
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
  expires_at > ?
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
  session_string = ?

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
FROM sessions WHERE session_id = ?

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
FROM sessions WHERE session_string = ?

-- name: CountSessions
-- access: read
-- rows: one
SELECT
  COUNT(*)
FROM sessions
WHERE
  expires_at > ?

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

-- name: SchemaSessions
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS sessions (
  session_string TEXT PRIMARY KEY,
  session_id BLOB UNIQUE NOT NULL,
  host_pubkey BLOB NOT NULL,
  password_hash TEXT,
  max_participants INTEGER DEFAULT 4,
  current_participants INTEGER DEFAULT 0,
  capabilities INTEGER DEFAULT 3,
  has_password INTEGER DEFAULT 0,
  expose_ip_publicly INTEGER DEFAULT 0,
  session_type INTEGER DEFAULT 0,
  server_address TEXT,
  server_port INTEGER DEFAULT 0,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  last_activity_at INTEGER NOT NULL,
  initiator_id BLOB,
  host_established INTEGER DEFAULT 0,
  host_participant_id BLOB,
  host_address TEXT,
  host_port INTEGER DEFAULT 0,
  host_connection_type INTEGER DEFAULT 0,
  in_migration INTEGER DEFAULT 0,
  migration_start_ns INTEGER DEFAULT 0
);

-- name: SchemaParticipants
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS participants (
  participant_id BLOB PRIMARY KEY,
  session_string TEXT NOT NULL,
  identity_pubkey BLOB NOT NULL,
  joined_at INTEGER NOT NULL,
  FOREIGN KEY (session_string) REFERENCES sessions(session_string) ON DELETE CASCADE
);

-- name: SchemaRateEvents
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS rate_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ip_address TEXT NOT NULL,
  event_type TEXT NOT NULL,
  timestamp INTEGER NOT NULL
);

-- name: SchemaIdxSessionsId
-- access: write
-- rows: zero
CREATE INDEX IF NOT EXISTS idx_sessions_id ON sessions(session_id);

-- name: SchemaIdxSessionsActivity
-- access: write
-- rows: zero
CREATE INDEX IF NOT EXISTS idx_sessions_activity ON sessions(last_activity_at);

-- name: SchemaIdxParticipantsSession
-- access: write
-- rows: zero
CREATE INDEX IF NOT EXISTS idx_participants_session ON participants(session_string);

-- name: SchemaIdxRateEvents
-- access: write
-- rows: zero
CREATE INDEX IF NOT EXISTS idx_rate_events ON rate_events(ip_address, event_type, timestamp);

-- name: SchemaKeyPolicy
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_key_policy (
  session_string TEXT PRIMARY KEY REFERENCES sessions(session_string) ON DELETE CASCADE,
  restricted INTEGER NOT NULL CHECK(restricted IN (0, 1))
);

-- name: SchemaIdentityKeys
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_identity_keys (
  session_string TEXT NOT NULL REFERENCES sessions(session_string) ON DELETE CASCADE,
  key_type TEXT NOT NULL CHECK(key_type = 'ed25519'),
  identity_pubkey BLOB NOT NULL CHECK(typeof(identity_pubkey) = 'blob' AND length(identity_pubkey) = 32),
  key_version INTEGER NOT NULL,
  added_at INTEGER NOT NULL,
  revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0, 1)),
  PRIMARY KEY(session_string, key_type, identity_pubkey)
);

-- name: BackfillSessionIdentityKeys
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_identity_keys
SELECT s.session_string, 'ed25519', s.host_pubkey, 0, s.created_at, 0
FROM sessions s
WHERE typeof(s.host_pubkey) = 'blob'
  AND length(s.host_pubkey) = 32
  AND s.host_pubkey != zeroblob(32)
  AND NOT EXISTS (SELECT 1 FROM session_key_policy p WHERE p.session_string = s.session_string);

-- name: BackfillSessionKeyPolicy
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_key_policy SELECT session_string, 0 FROM sessions;

-- name: ImportLegacySessionKeys
-- access: write
-- rows: zero
INSERT OR IGNORE INTO session_identity_keys
SELECT k.session_string, 'ed25519', k.identity_pubkey, k.key_version, k.added_at,
  CASE WHEN k.revoked = 0 THEN 0 ELSE 1 END
FROM session_keys k
WHERE typeof(k.identity_pubkey) = 'blob'
  AND length(k.identity_pubkey) = 32
  AND NOT EXISTS (SELECT 1 FROM session_key_policy p WHERE p.session_string = k.session_string);

-- name: HasLegacySessionKeys
-- access: read
-- rows: zero-or-one
SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'session_keys'

-- name: InsertSessionKeyPolicy
-- access: write
-- rows: zero
INSERT INTO session_key_policy VALUES (?, ?)

-- name: GetSessionKeyPolicy
-- access: read
-- rows: zero-or-one
SELECT restricted FROM session_key_policy WHERE session_string = ?

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

-- name: SchemaSessionTransports
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_transports (
  session_id BLOB PRIMARY KEY REFERENCES sessions(session_id) ON DELETE CASCADE,
  metadata BLOB NOT NULL
);

-- name: SetSessionTransports
-- access: write
-- rows: zero
INSERT OR REPLACE INTO session_transports(session_id, metadata) VALUES (?, ?);

-- name: SchemaSessionTransportHosts
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_transport_hosts (
  session_id BLOB PRIMARY KEY REFERENCES session_transports(session_id) ON DELETE CASCADE,
  host_id BLOB NOT NULL CHECK(length(host_id) = 16)
);

-- name: SchemaClearTransportsTrigger
-- access: write
-- rows: zero
CREATE TRIGGER IF NOT EXISTS clear_session_transports_after_host_clear
AFTER UPDATE OF host_established ON sessions
WHEN OLD.host_established = 1 AND NEW.host_established = 0
BEGIN
  DELETE FROM session_transports WHERE session_id = NEW.session_id;
END;

-- name: SetSessionTransportHost
-- access: write
-- rows: zero
INSERT OR REPLACE INTO session_transport_hosts(session_id, host_id) VALUES (?, ?);

-- name: GetSessionTransports
-- access: read
-- rows: zero-or-one
SELECT t.metadata
FROM session_transports t
JOIN sessions s ON s.session_id = t.session_id
LEFT JOIN session_transport_hosts h ON h.session_id = t.session_id
WHERE t.session_id = ?
  AND s.in_migration = 0
  AND COALESCE(s.host_participant_id, s.initiator_id) IS COALESCE(h.host_id, s.initiator_id);
