-- name: CountRateEvents
-- access: read
-- rows: one
SELECT
  COUNT(*)
FROM rate_events
WHERE
  ip_address = ?
  AND event_type = ?
  AND timestamp >= ?

-- name: InsertRateEvent
-- access: write
-- rows: zero
INSERT INTO rate_events (ip_address, event_type, timestamp)
VALUES
  (?, ?, ?)

-- name: DeleteRateEvents
-- access: write
-- rows: zero
DELETE FROM rate_events
WHERE
  timestamp < ?
