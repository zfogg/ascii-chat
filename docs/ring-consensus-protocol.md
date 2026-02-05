# Ring Consensus Protocol for ACDS

**Version**: 1.0
**Date**: February 2026
**Author**: Protocol Design - ascii-chat Discovery Service
**Status**: DRAFT - Ready for Implementation

---

## Overview

The **Ring Consensus Protocol** is a distributed consensus mechanism for determining the best host and backup host in a multi-participant ascii-chat session. Unlike centralized election models, this protocol:

- Distributes network quality data collection around a logical ring
- Gives the ring leader (last person) complete visibility into all metrics
- Allows informed, deterministic host election based on full knowledge
- Ensures all participants trust and honor the decision
- Handles dynamic participant changes (join/leave) with seamless ring reformation

---

## Core Concepts

### Ring Topology

All clients in a session are arranged in a **logical ring**, ordered by participant ID. The ring is completely deterministic - given a set of participant IDs, any client can compute the same ring order.

```
         Client A
        /         \
       /           \
   Client B ----- Client D
       \           /
        \         /
         Client C

Ring order: A → B → C → D → A (circularly back to A)
```

**Key Properties:**
- Order is deterministic (based on participant IDs sorted lexicographically)
- Participants know their position (which client is "next" and which is "previous")
- No single point of failure in the ring structure itself
- Adding/removing clients automatically reforms the ring

### Network Quality Metrics (Per Participant)

Each participant measures and shares these metrics:

```c
typedef struct {
  uint8_t participant_id[16];      // Who measured this

  // NAT characteristics
  uint8_t nat_tier;                // 0=LAN, 1=Public, 2=UPnP, 3=STUN, 4=TURN
  uint32_t upload_kbps;            // Upload bandwidth in Kbps
  uint16_t rtt_to_host_ms;         // Latency to current host
  uint8_t stun_probe_success_pct;  // STUN probe success rate (0-100%)

  // Connectivity info
  char public_address[64];         // Public/detected IP
  uint16_t public_port;            // Public port if applicable
  uint8_t connection_type;         // Direct/UPnP/STUN/TURN

  // Timestamp
  uint64_t measurement_time_ms;    // When measured (Unix ms)
  uint32_t measurement_window_ms;  // Duration of measurement
} participant_metrics_t;
```

**Metric Details:**
- **NAT tier**: Determined during initial STUN probing (0=LAN best, 4=TURN worst)
- **Upload bandwidth**: Estimated from available network speed or measured via bandwidth test
- **RTT to host**: Ping latency measured from periodic keepalive packets
- **STUN probe success rate**: Percentage of STUN binding requests that received responses (reflects network stability/loss on UDP path)

### Ring Leader

The **ring leader** is the last participant in the ring order - the one whose "next" pointer wraps around back to the first participant.

**Responsibilities:**
1. Receive all metrics from previous participant in ring
2. Add own metrics to the collection
3. Compute which client should be host and backup
4. Broadcast decision to all via server
5. All clients immediately accept and use the new host/backup addresses

**Why the ring leader?**
- Naturally receives all data without additional hops
- Single point of decision (no quorum delays, no consensus rounds)
- All other clients trust the leader because they KNOW the leader has complete information
- Fast: O(n) hops to collect from all n participants

---

## Protocol Phases

### Phase 1: Ring Formation

**When**: Session starts OR participant joins/leaves
**Performed by**: Server (ACDS)

1. Server maintains ordered list of active participant IDs
2. Server broadcasts `RING_MEMBERS` packet to all clients with:
   - Complete participant list (ordered)
   - Each client's index in the ring
   - Next and previous participant IDs
3. Each client independently computes same ring order
4. Ring leader identified as `participants[n-1]` (last one)

```
Server broadcasts:
{
  session_id: [16 bytes],
  participant_ids: [A_id, B_id, C_id, D_id],  // Sorted order
  ring_positions: {
    A: {next: B, prev: D, position: 0, is_leader: false},
    B: {next: C, prev: A, position: 1, is_leader: false},
    C: {next: D, prev: B, position: 2, is_leader: false},
    D: {next: A, prev: C, position: 3, is_leader: true},   // Ring leader!
  }
}
```

### Phase 2: Metrics Collection Round

**When**: Every 5 minutes OR after new participant joins
**Initiated by**: Ring leader
**Duration**: ~30 seconds to 2 minutes (network dependent)

#### Step 1: Leader Sends Collection Request

Ring leader sends `STATS_COLLECTION_START` packet to previous participant:

```
{
  session_id: [16 bytes],
  round_id: 1,                              // Monotonic counter
  collection_deadline_ms: [deadline],       // When to stop collecting
  ring_order: [A_id, B_id, C_id, D_id]     // Explicit ring for verification
}
```

#### Step 2: Metrics Flow Around Ring

Each participant:
1. **Measures own metrics** (NAT tier, upload speed, RTT, loss, etc.)
2. **Sends `STATS_UPDATE` to next in ring** with:
   - Own metrics (newly measured)
   - All metrics received from previous (full collection so far)
   - Round ID to track collection instance

```
Flow visualization:
A measures, sends to B:
  STATS_UPDATE { round_id=1, metrics=[A] }

B measures, receives from A, sends to C:
  STATS_UPDATE { round_id=1, metrics=[A, B] }

C measures, receives from B, sends to D:
  STATS_UPDATE { round_id=1, metrics=[A, B, C] }

D measures, receives from C, processes locally:
  STATS_UPDATE { round_id=1, metrics=[A, B, C, D] }  ← LEADER HAS ALL!
```

**Wire Format (STATS_UPDATE packet):**

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t sender_id[16];          // Who is sending this around the ring
  uint32_t round_id;              // Collection round number
  uint8_t num_metrics;            // Number of participant metrics in collection
  // Followed by: participant_metrics_t[num_metrics]
} stats_update_packet_t;
```

#### Step 3: Leader Computation (After All Metrics Received)

Once leader receives metrics from all participants (within deadline), compute:

1. **Score each participant** using deterministic formula:
   ```
   score = (4 - nat_tier) * 1000      // Lower NAT tier = higher score
           + (upload_kbps / 10)         // Higher bandwidth = higher score
           + (500 - rtt_ms)             // Lower latency = higher score
           + stun_probe_success_pct     // Higher success rate = higher score

   // Higher score = better (sort descending, take first)
   ```

2. **Determine host & backup:**
   - Sort participants by score (ascending)
   - First participant (best score) = new host
   - Second participant (second best) = backup host

3. **Check if change needed:**
   - Compare new host with current host
   - If different: broadcast HOST_CHANGED
   - If same: broadcast HOST_CONFIRMED (still the same, but have backup)

### Phase 3: Leader Announcement

Ring leader broadcasts `RING_ELECTION_RESULT` to all clients via server:

```
{
  session_id: [16 bytes],
  round_id: 1,
  host_id: [D_id],           // New/current host
  host_address: "192.168.1.5",
  host_port: 27224,
  backup_host_id: [C_id],    // Backup if main dies
  backup_address: "192.168.1.4",
  backup_port: 27224,
  election_timestamp_ms: [time],
  all_metrics: [metrics for A, B, C, D]  // Include data so all can verify
}
```

### Phase 4: Participant Acknowledgment

All participants:
1. Receive `RING_ELECTION_RESULT` from server
2. Verify leader is the expected ring leader (cross-check)
3. Store host and backup addresses locally
4. If host changed: trigger migration to new host
5. Send `STATS_ACK` back to server confirming receipt

```
{
  session_id: [16 bytes],
  participant_id: [my_id],
  round_id: 1,
  ack_status: ACCEPTED,        // or REJECTED if something wrong
  stored_host_id: [D_id],      // Confirms what we're storing
  stored_backup_id: [C_id]
}
```

---

## Handling Participant Changes

### When Participant Joins

**Timeline:**
1. New participant connects to ACDS
2. Server broadcasts new `RING_MEMBERS` packet
3. All clients recompute ring positions
4. If new participant is "closer to leader" than old leader, leadership may shift
5. Existing metrics can be carried over (or request fresh collection)

**Key point:** Ring reforms automatically - no explicit "rejoin ring" needed

### When Participant Leaves

**Timeline:**
1. Server detects participant disconnect
2. Broadcasts new `RING_MEMBERS` with reduced list
3. Ring shrinks: if participant was leader, new last one becomes leader
4. If leader left: leadership auto-transfers to new last participant
5. Next metrics collection round proceeds with new ring

**Example:**
```
Before: A → B → C → D (D is leader)
        Leader: D

D crashes:
After:  A → B → C (C becomes new leader)
        Leader: C
```

### Host Death During Collection

**Scenario:** Host dies mid-collection (e.g., metrics flow: A→B→C→[host]→?)

**Behavior:**
1. Ring leader notices host hasn't sent/received for >timeout
2. Doesn't block collection - continues with available metrics
3. In election: host that died gets infinite score (won't be elected)
4. Backup host from previous round immediately becomes active
5. Next collection round includes new host

---

## Election Algorithm (Deterministic)

**Critical:** All clients must compute identical results when given same input

### Scoring Function

```
def compute_score(metrics):
    # NAT tier: 0=best, 4=worst (negate so higher = better)
    nat_score = (4 - metrics.nat_tier) * 1000

    # Bandwidth: higher is better
    bw_score = metrics.upload_kbps / 10

    # Latency: lower is better (negate)
    rtt_score = 500 - metrics.rtt_to_host_ms

    # STUN probe success: higher is better (0-100%)
    probe_score = metrics.stun_probe_success_pct

    # Total (higher = better)
    total = nat_score + bw_score + rtt_score + probe_score
    return total
```

### Tie-Breaking

If two participants have identical scores:
1. Compare NAT tier (lower wins)
2. Compare upload bandwidth (higher wins)
3. Compare participant ID lexicographically (A > Z = wins)

### Verification by All Clients

Each participant can independently verify the election:

```python
# Given the metrics from RING_ELECTION_RESULT

scores = {}
for participant_id, metrics in all_metrics.items():
    scores[participant_id] = compute_score(metrics)

sorted_by_score = sort(scores)  # Ascending by score

# Verify what leader announced matches our computation
assert sorted_by_score[0] == announced_host_id
assert sorted_by_score[1] == announced_backup_id
```

---

## State Management

### Server Side (ACDS)

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_ids[MAX_PARTICIPANTS][16];  // Ring order
  int num_participants;

  // Ring info
  uint8_t ring_leader_id[16];                     // Last in order

  // Current host info
  uint8_t current_host_id[16];
  char current_host_address[64];
  uint16_t current_host_port;

  // Backup info
  uint8_t current_backup_id[16];

  // Collection state
  uint32_t collection_round;                      // Counter for rounds
  uint64_t last_collection_time_ms;               // When last round started
} session_state_t;
```

### Client Side (Discovery Mode)

```c
typedef struct {
  // Ring position info
  uint8_t my_id[16];
  uint8_t next_participant_id[16];                // Send metrics to them
  uint8_t prev_participant_id[16];                // Receive metrics from them
  int ring_position;                              // 0, 1, 2, ...
  bool am_ring_leader;

  // Host info (for failover)
  uint8_t current_host_id[16];
  char current_host_address[64];
  uint16_t current_host_port;

  uint8_t backup_host_id[16];
  char backup_host_address[64];
  uint16_t backup_host_port;

  // Last election result (for verification)
  uint32_t last_round_id;
  participant_metrics_t all_metrics[MAX_PARTICIPANTS];
  int num_metrics_in_last_round;
} client_ring_state_t;
```

---

## New Packet Types

### RING_MEMBERS

Server → All clients (broadcast when ring changes)

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_ids[MAX_PARTICIPANTS][16];  // Ring order (lexicographic)
  uint8_t num_participants;
  uint8_t ring_leader_index;                       // Index of leader in participant_ids
  uint32_t generation;                             // Incremented on each ring change
} acip_ring_members_t;
```

### STATS_COLLECTION_START

Ring leader → Previous in ring (initiates metrics collection)

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t initiator_id[16];                       // Ring leader
  uint32_t round_id;                              // Collection round counter
  uint64_t collection_deadline_ms;                // Unix ms when collection must finish
} acip_stats_collection_start_t;
```

### STATS_UPDATE

Any → Next in ring (pass metrics around ring)

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t sender_id[16];                          // Who's sending this packet
  uint32_t round_id;
  uint8_t num_metrics;                            // Count of metrics in this packet
  // Followed by: participant_metrics_t metrics[num_metrics]
} acip_stats_update_t;

typedef struct {
  uint8_t participant_id[16];
  uint8_t nat_tier;
  uint32_t upload_kbps;
  uint16_t rtt_ms;
  uint8_t stun_probe_success_pct;   // 0-100: percentage of STUN probes that succeeded
  char public_address[64];
  uint16_t public_port;
  uint8_t connection_type;
  uint64_t measurement_time_ms;
  uint32_t measurement_window_ms;
} participant_metrics_t;
```

### RING_ELECTION_RESULT

Ring leader → All clients via server (announces decision)

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t leader_id[16];                          // Who made this decision
  uint32_t round_id;

  // New host
  uint8_t host_id[16];
  char host_address[64];
  uint16_t host_port;

  // Backup host
  uint8_t backup_id[16];
  char backup_address[64];
  uint16_t backup_port;

  uint64_t elected_at_ms;
  uint8_t num_participants;
  // Followed by: participant_metrics_t metrics[num_participants]
} acip_ring_election_result_t;
```

### STATS_ACK

Any participant → Server (confirms receipt of election)

```c
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];
  uint32_t round_id;
  uint8_t ack_status;                             // ACCEPTED/REJECTED
} acip_stats_ack_t;
```

---

## Failover Behavior

### Host Death During Active Session

**Timeline:**
```
T=0s:     Host alive, all connected
T=5m:     Ring consensus round: Host elected, Backup=C stored
T=8m:     Host dies (TCP connection drops)
T=8.05s:  All participants detect disconnect (TCP RST)
T=8.10s:  Backup (C) starts listening on stored address
T=8.15s:  Participants connect to backup
T=8.30s:  WebRTC negotiation
T=8.50s:  Media resumed on new host
```

**Key:** All clients already know backup address from election, no ACDS query needed

### Ring Leader Dies During Collection

**Example:**
- Collection round in progress: A→B→C→[waiting for D]
- D (ring leader) dies mid-collection
- C has collected A, B, C metrics (missing D)
- Options:
  1. **Continue:** C becomes new leader, elects based on available data
  2. **Restart:** Wait for ring reform, start new collection round
  3. **Timeout:** After deadline, whatever leader received becomes final

**Recommendation:** Option 1 (continue) - graceful degradation

---

## Implementation Notes

### Deterministic Ordering

Ring must be identical on all clients. Use **lexicographic sorting** of participant IDs:

```python
ring_order = sorted(participant_ids)  # Sort IDs as bytes
for i in range(len(ring_order)):
    current = ring_order[i]
    next_idx = (i + 1) % len(ring_order)
    next = ring_order[next_idx]
```

### Collection Timeout

If metrics don't arrive within deadline, leader proceeds with what it has:

```c
#define STATS_COLLECTION_TIMEOUT_MS 30000  // 30 seconds
```

### Collection Frequency

Run full metrics collection every:

```c
#define RING_CONSENSUS_INTERVAL_MS (5 * 60 * 1000)  // 5 minutes
```

Or on-demand when:
- Participant joins
- Participant leaves
- Server detects poor quality from host
- Manual trigger (user-initiated)

### Measuring STUN Probe Success Rate

**Why STUN probes?**
- TCP (used for metrics collection) is reliable and doesn't show packet loss
- STUN probes use UDP, which reflects actual network conditions on the direct path
- Already part of NAT detection, so reusing existing infrastructure
- Gives insight into network stability independent of TCP retransmission

**Implementation:**
```c
// During metrics collection window:
for (int i = 0; i < 10; i++) {
    send_stun_binding_request(stun_server);
    // Wait for response with timeout
    if (receive_stun_response(...)) {
        success_count++;
    }
}
metrics.stun_probe_success_pct = (success_count * 100) / 10;  // 0-100%
```

### Metrics Measurement Window

Each participant should measure during:

```c
#define METRICS_WINDOW_MS 60000  // Measure over 60 seconds of media flow
```

Capture:
- STUN probe success rate: Send ~10 STUN binding requests during measurement window, track responses (UDP-based, reflects network quality)
- Upload bandwidth: Estimate from available connection quality or measure via bandwidth test probes
- RTT: Measure from ping packets or media frame timestamps (median over measurement window)
- All measured during actual media transmission period (not synthetic tests)

---

## Security Considerations

### Trust Model

- All participants **trust the ring leader** because they know leader has complete information
- **Verification:** Any participant can independently compute same election result
- **No authorization needed:** Ring position is deterministic, not assigned
- **No secrets:** Metrics are visible to all (can add encryption at transport layer if needed)

### Attack Vectors

| Attack | Mitigation |
|--------|-----------|
| Ring leader lies about metrics | All clients verify independently |
| Ring leader offline | Leadership auto-transfers to new last |
| Participant falsifies metrics | Other participants can challenge/remeasure |
| Man-in-the-middle on election | Crypto handshake happens after election |
| Participant spams collection | Timeout + rate limiting |

### Optional: Signed Election

If stronger guarantees needed, ring leader can sign the election result:

```c
acip_ring_election_result_t {
  // ... fields ...
  uint8_t signature[64];  // Ed25519 signature of above fields
}
```

Each participant verifies signature using leader's public key.

---

## Comparison to Alternatives

### vs. Centralized Host Election (Current)

| Aspect | Centralized | Ring Consensus |
|--------|-------------|----------------|
| **Decision maker** | Always host | Rotates (last in ring) |
| **Information access** | Limited to host's metrics | Complete (all metrics reach leader) |
| **Failure recovery** | Host dies = election delay | Automatic leadership transfer |
| **Complexity** | Simple | Medium |
| **Scalability** | O(1) computation, O(n) to collect | O(n) hops to collect, O(n) to sort |
| **Fairness** | Host may be biased | Leader computes fairly with all data |

### vs. Byzantine Fault Tolerance

| Aspect | BFT | Ring Consensus |
|--------|-----|----------------|
| **Fault tolerance** | (n-1)/3 malicious nodes | Assumes honest leader |
| **Rounds** | O(f) rounds, f=faults | 1 round + 1 round trip |
| **Message complexity** | O(n²) | O(n) |
| **Leader rotation** | Automatic on f leaders dead | Automatic (ring order) |
| **Implementation** | Very complex | Medium complexity |

---

## Example: 4-Participant Election

**Setup:**
```
Participants: A, B, C, D
Ring order: A → B → C → D → A (D is leader)

Session active: A hosting

Metrics collected:
  A: NAT_tier=1 (public), upload=50Mbps, rtt=30ms, stun_success=95%
  B: NAT_tier=3 (STUN), upload=10Mbps, rtt=50ms, stun_success=85%
  C: NAT_tier=2 (UPnP), upload=100Mbps, rtt=20ms, stun_success=98%
  D: NAT_tier=1 (public), upload=75Mbps, rtt=25ms, stun_success=96%
```

**Scoring:**
```
Score(A) = (4-1)*1000 + (50000/10) + (500-30) + 95
         = 3000 + 5000 + 470 + 95 = 8565

Score(B) = (4-3)*1000 + (10000/10) + (500-50) + 85
         = 1000 + 1000 + 450 + 85 = 2535

Score(C) = (4-2)*1000 + (100000/10) + (500-20) + 98
         = 2000 + 10000 + 480 + 98 = 12578

Score(D) = (4-1)*1000 + (75000/10) + (500-25) + 96
         = 3000 + 7500 + 475 + 96 = 11071
```

**Election:**
```
Sorted by score (descending - higher is better):
  Best:   C (12578)  - Excellent bandwidth, good NAT, low latency, high probe success
  Backup: D (11071)  - Good balanced metrics, public NAT
  3rd:    A (8565)   - Public NAT but lower bandwidth
  4th:    B (2535)   - Poor metrics (STUN NAT tier, low bandwidth, high latency)

New host: C (best overall quality)
Backup:   D (next best, can handle load if C fails)
```

**Broadcast:**
```
RING_ELECTION_RESULT {
  round_id: 1,
  host_id: C,
  host_address: 192.168.1.3,
  backup_id: D,
  backup_address: 192.168.1.4,
  metrics: [A, B, C, D with all details]
}

All participants verify:
  Sort(metrics) = [C, D, ...] ✓ Matches announcement ✓
  Store: host=C, backup=D
```

---

## Implementation Roadmap

### Phase 1: Ring Formation (Week 1)
- [ ] Add RING_MEMBERS packet type
- [ ] Server tracks participant order
- [ ] Clients compute ring position on RING_MEMBERS receipt
- [ ] All clients agree on leader

### Phase 2: Metrics Collection (Week 2)
- [ ] Add STATS_UPDATE packet
- [ ] Implement metrics measurement (NAT, upload, latency, loss)
- [ ] Ring leader starts collection round
- [ ] Metrics flow around ring
- [ ] Leader receives all

### Phase 3: Election & Broadcasting (Week 3)
- [ ] Add scoring algorithm
- [ ] Leader computes best host/backup
- [ ] Add RING_ELECTION_RESULT packet
- [ ] Broadcast to all
- [ ] Clients verify and store

### Phase 4: Migration Integration (Week 4)
- [ ] Trigger migration when host changes
- [ ] Use announced backup address on failover
- [ ] Handle mid-collection host death
- [ ] Test with 3+ participants

### Phase 5: Testing & Polish (Week 5)
- [ ] Unit tests for scoring
- [ ] Integration tests: 2/3/4/5 participants
- [ ] Ring member changes (join/leave)
- [ ] Leader death during collection
- [ ] Performance profiling

---

## Conclusion

The Ring Consensus Protocol provides a fair, distributed, and scalable way to elect the best host in multi-user ascii-chat sessions. By giving the ring leader complete information and having all participants verify the decision, we achieve:

✅ Fairness - Decision based on complete knowledge
✅ Resilience - Leadership auto-transfers
✅ Speed - Single round to collect + compute
✅ Scalability - O(n) complexity
✅ Simplicity - Deterministic, easy to verify

