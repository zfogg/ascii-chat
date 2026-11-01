# Discovery host designation

ACDS is authoritative about the current host. Host election and quality policy
remain in the existing handoff implementation.

A joined connection may announce only its own participant ID in its own session.
ACDS accepts the announcement only when its host ID and generation match the
current handoff authority. Before a handoff exists, authority is initialized from
the persisted host, or the session initiator. The endpoint must be terminated,
have a nonzero port and a known connection type. An empty address uses the
connection source address, as session creation already does. Once accepted, an endpoint is
immutable within a generation; a conflicting replacement is rejected. An exact
duplicate can repeat delivery.

Authority validation, database updates and fanout are serialized with handoff
commits using the existing handoff mutex. HOST_DESIGNATED goes to every joined
connection in the session, including the announcing host, through the existing
pinned registry visitor. Other sessions and connections that have not joined are
excluded. Individual failed sends are logged without stopping delivery to others.
A subsequent announcement can repair delivery; a joining/reconnecting participant
receives the persisted designation immediately after SESSION_JOINED.

Addresses follow the same disclosure rule as SESSION_JOINED: only password
protected sessions or sessions with explicit public IP exposure disclose them.
WebRTC participants can reconcile by participant ID without an address.

HOST_ANNOUNCEMENT and HOST_DESIGNATED include a 32-bit handoff generation in
network byte order, initially one. The extended payload requires matching
client/server versions; old announcement sizes fail closed. The generation is
scoped to the active ACDS handoff lifetime, not a durable epoch across ACDS restarts.

Clients consume designations on the ACDS signaling transport, validate exact
size, session, endpoint and generation, and ignore stale or duplicate authority.
An endpoint-less designation may be completed by an endpoint for the same host
and generation. Repeated handoff commits also cannot queue another teardown.
An unchanged host updates metadata without restarting healthy media. A changed
host uses the existing deferred role dispatcher, which joins media workers before
releasing transports. TCP participants wait for a disclosed replacement endpoint;
WebRTC participants can switch by host ID.

An actual host change uses the shared NOTICE_ANNOUNCE component with “The session
has a new host.” It does not capture input, require acknowledgment, or introduce
a modal popup over live video. Initial joins and duplicate events stay quiet.
This is related to #280 (popup windows) and #297 (OS notifications). It does
not implement a generic popup/help refactor or platform OS notifications.
