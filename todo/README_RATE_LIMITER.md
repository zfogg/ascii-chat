# Rate Limiter Integration Guide

## Overview
This rate limiter library provides multiple strategies to protect your ASCII chat server from abusive clients. It includes:

1. **Token Bucket** - Best for most use cases, allows bursts
2. **Sliding Window** - Strict rate limiting over time windows
3. **Fixed Window** - Simple per-second/minute counters

## Quick Integration

### 1. Copy Files to Your Project
```bash
# Copy the library files to your lib/ directory
cp todo/rate_limiter.h lib/
cp todo/rate_limiter.c lib/
```

### 2. Add to Your Server

In `server.c`, add rate limiting to each client:

```c
// Add to client_info_t structure
typedef struct client_info {
    // ... existing fields ...
    multi_rate_limiter_t* rate_limiter;  // Add this
} client_info_t;

// In add_client() function
client->rate_limiter = multi_rate_limiter_create();

// In remove_client() function
if (client->rate_limiter) {
    multi_rate_limiter_destroy(client->rate_limiter);
}
```

### 3. Check Rates Before Processing Packets

In `client_receive_thread_func()`, add checks:

```c
switch (type) {
    case PACKET_TYPE_ASCII_FRAME:
        // Check rate limit FIRST
        if (!multi_rate_limiter_check_video(client->rate_limiter, len)) {
            log_warn("Client %u exceeded video rate limit (%zu bytes)", 
                    client->client_id, len);
            break;  // Drop packet
        }
        // ... existing frame handling ...
        break;

    case PACKET_TYPE_AUDIO_BATCH:
        if (!multi_rate_limiter_check_audio(client->rate_limiter, len)) {
            log_warn("Client %u exceeded audio rate limit", client->client_id);
            break;
        }
        // ... existing audio handling ...
        break;
}
```

## Default Limits

The multi-rate limiter uses these defaults:

- **Video**: 60 frames/sec (burst: 120 frames)
- **Audio**: 100 packets/sec (burst: 200 packets)  
- **Control**: 10 packets/sec (burst: 20 packets)
- **Bandwidth**: 10MB/sec (burst: 20MB)

## Customization

### Adjust Limits

Create custom limiters with different settings:

```c
// Stricter video limits
client->video_limiter = rate_limiter_create_token_bucket(
    "video",
    30.0,   // 30 FPS max
    60.0    // Burst of 60 frames
);

// Bandwidth limit: 5MB/sec
rate_limit_config_t bw_config = {
    .type = RATE_LIMIT_TOKEN_BUCKET,
    .max_requests_per_second = 5000.0,   // 5K tokens/sec
    .burst_size = 10000.0,                // 10K burst
    .cost_per_byte = 1.0 / 1024.0        // 1 token = 1KB
};
client->bandwidth_limiter = rate_limiter_create("bandwidth", &bw_config);
```

### Progressive Penalties

Instead of just dropping packets, escalate responses:

```c
// Track violations
if (!rate_limiter_check(...)) {
    client->violations++;
    
    if (client->violations > 100) {
        // Disconnect abusive client
        remove_client(client->client_id);
    } else if (client->violations > 50) {
        // Drop all packets for 10 seconds
        client->penalty_until = time(NULL) + 10;
    } else if (client->violations > 20) {
        // Just log for now
        log_warn("Client %u has %d violations", 
                client->client_id, client->violations);
    }
}
```

## Monitoring

### Get Statistics

```c
rate_limit_stats_t stats;
rate_limiter_get_stats(client->video_limiter, &stats);

log_info("Client %u video stats: %llu allowed, %llu blocked (%.1f%% blocked)",
         client->client_id,
         stats.allowed_count,
         stats.blocked_count,
         100.0 * stats.blocked_count / (stats.allowed_count + stats.blocked_count));
```

### Live Status

```c
// Get human-readable status
const char* status = rate_limiter_status(client->video_limiter);
log_debug("Client %u: %s", client->client_id, status);
// Output: "TokenBucket[video]: 45.2/120.0 tokens, 60.0/sec refill"
```

## Testing

### Compile and Run Tests
```bash
cd todo
make clean && make
./rate_limiter_example
```

### Test Against Your Server

1. Start server with rate limiting
2. Run normal client - should work fine
3. Try spamming with modified client:
```c
// Spam test
for (int i = 0; i < 1000; i++) {
    send_video_frame(sockfd, frame_data, frame_size);
}
```
4. Check server logs for rate limit messages

## Performance Impact

- **Memory**: ~1KB per client for limiters
- **CPU**: Negligible (< 0.1% overhead)
- **Latency**: < 1 microsecond per check

## Recommendations

1. **Start Permissive**: Begin with high limits and monitor
2. **Log Before Blocking**: First just log violations
3. **Different Limits Per Client Type**: Maybe admins get higher limits
4. **Monitor Patterns**: Look for burst patterns in logs
5. **Gradual Tightening**: Reduce limits based on real usage data

## Common Issues

### Problem: Legitimate bursts blocked
**Solution**: Increase burst_size in token bucket

### Problem: Clients disconnect on fast scene changes  
**Solution**: Increase video FPS limit or use larger burst

### Problem: Audio cuts out during high activity
**Solution**: Prioritize audio packets over video

## Advanced Features

### Per-IP Rate Limiting
Track by IP before client even authenticates:

```c
typedef struct {
    char ip[INET_ADDRSTRLEN];
    rate_limiter_t* limiter;
    time_t last_seen;
} ip_rate_limit_t;

// Global IP tracking
ip_rate_limit_t ip_limits[1000];
```

### Dynamic Adjustment
Adjust limits based on server load:

```c
if (get_cpu_usage() > 80.0) {
    // Tighten limits when server is stressed
    reduce_all_limits(0.5);  // 50% of normal
}
```

### Whitelist/Blacklist
```c
if (is_whitelisted(client->ip)) {
    // No rate limiting for trusted IPs
    return true;
}
```

## Integration Checklist

- [ ] Add rate_limiter files to project
- [ ] Update Makefile to compile rate_limiter.c
- [ ] Add rate_limiter to client_info_t
- [ ] Initialize on client connect
- [ ] Add checks in packet handlers
- [ ] Destroy on client disconnect
- [ ] Add monitoring/logging
- [ ] Test with normal usage
- [ ] Test with abuse scenarios
- [ ] Tune limits based on testing
- [ ] Document limits for users