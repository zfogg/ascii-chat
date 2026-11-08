-- name: SchemaSessions
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS sessions (session_string TEXT PRIMARY KEY, session_id BLOB UNIQUE NOT NULL, host_pubkey BLOB NOT NULL, password_hash TEXT, max_participants INTEGER DEFAULT 4, current_participants INTEGER DEFAULT 0, capabilities INTEGER DEFAULT 3, has_password INTEGER DEFAULT 0, expose_ip_publicly INTEGER DEFAULT 0, session_type INTEGER DEFAULT 0, server_address TEXT, server_port INTEGER DEFAULT 0, created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, last_activity_at INTEGER NOT NULL, initiator_id BLOB, host_established INTEGER DEFAULT 0, host_participant_id BLOB, host_address TEXT, host_port INTEGER DEFAULT 0, host_connection_type INTEGER DEFAULT 0, in_migration INTEGER DEFAULT 0, migration_start_ns INTEGER DEFAULT 0);

-- name: SchemaParticipants
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS participants (participant_id BLOB PRIMARY KEY, session_string TEXT NOT NULL, identity_pubkey BLOB NOT NULL, joined_at INTEGER NOT NULL, FOREIGN KEY (session_string) REFERENCES sessions(session_string) ON DELETE CASCADE);

-- name: SchemaRateEvents
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS rate_events (id INTEGER PRIMARY KEY AUTOINCREMENT, ip_address TEXT NOT NULL, event_type TEXT NOT NULL, timestamp INTEGER NOT NULL);

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
CREATE TABLE IF NOT EXISTS session_key_policy (session_string TEXT PRIMARY KEY REFERENCES sessions(session_string) ON DELETE CASCADE, restricted INTEGER NOT NULL CHECK(restricted IN (0, 1)));

-- name: SchemaIdentityKeys
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_identity_keys (session_string TEXT NOT NULL REFERENCES sessions(session_string) ON DELETE CASCADE, key_type TEXT NOT NULL CHECK(key_type = 'ed25519'), identity_pubkey BLOB NOT NULL CHECK(typeof(identity_pubkey) = 'blob' AND length(identity_pubkey) = 32), key_version INTEGER NOT NULL, added_at INTEGER NOT NULL, revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0, 1)), PRIMARY KEY(session_string, key_type, identity_pubkey));

-- name: SchemaSessionTransports
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_transports (session_id BLOB PRIMARY KEY REFERENCES sessions(session_id) ON DELETE CASCADE, metadata BLOB NOT NULL);

-- name: SchemaSessionTransportHosts
-- access: write
-- rows: zero
CREATE TABLE IF NOT EXISTS session_transport_hosts (session_id BLOB PRIMARY KEY REFERENCES session_transports(session_id) ON DELETE CASCADE, host_id BLOB NOT NULL CHECK(length(host_id) = 16));

-- name: SchemaClearTransportsTrigger
-- access: write
-- rows: zero
CREATE TRIGGER IF NOT EXISTS clear_session_transports_after_host_clear AFTER UPDATE OF host_established ON sessions WHEN OLD.host_established = 1 AND NEW.host_established = 0 BEGIN DELETE
FROM session_transports
WHERE session_id = NEW.session_id;
END;
