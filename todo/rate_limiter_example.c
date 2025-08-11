/*
 * Rate Limiter Example and Test Program
 * 
 * Demonstrates how to integrate rate limiting into a network server
 * 
 * Compile with:
 *   gcc -o rate_limiter_example rate_limiter_example.c rate_limiter.c -lm
 * 
 * Run with:
 *   ./rate_limiter_example
 */

#include "rate_limiter.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

// Simulate different packet types from your ASCII chat
typedef enum {
    PACKET_VIDEO_FRAME,
    PACKET_AUDIO_BATCH,
    PACKET_CONTROL,
    PACKET_PING
} packet_type_t;

// Simulate a client connection
typedef struct {
    int client_id;
    char name[32];
    multi_rate_limiter_t* rate_limiter;
    
    // Statistics
    int packets_sent;
    int packets_blocked;
} client_t;

// Simulate receiving a packet
void simulate_packet(client_t* client, packet_type_t type, size_t size) {
    const char* type_name;
    bool allowed = false;
    
    switch (type) {
        case PACKET_VIDEO_FRAME:
            type_name = "VIDEO";
            allowed = multi_rate_limiter_check_video(client->rate_limiter, size);
            break;
            
        case PACKET_AUDIO_BATCH:
            type_name = "AUDIO";
            allowed = multi_rate_limiter_check_audio(client->rate_limiter, size);
            break;
            
        case PACKET_CONTROL:
        case PACKET_PING:
            type_name = "CONTROL";
            allowed = multi_rate_limiter_check_control(client->rate_limiter, size);
            break;
    }
    
    if (allowed) {
        client->packets_sent++;
        printf("[ALLOW] Client %d: %s packet (%zu bytes)\n", 
               client->client_id, type_name, size);
    } else {
        client->packets_blocked++;
        printf("[BLOCK] Client %d: %s packet (%zu bytes) - RATE LIMITED\n", 
               client->client_id, type_name, size);
    }
}

// Test 1: Normal client behavior
void test_normal_client(void) {
    printf("\n=== Test 1: Normal Client Behavior ===\n");
    
    client_t client = {
        .client_id = 1,
        .rate_limiter = multi_rate_limiter_create(),
        .packets_sent = 0,
        .packets_blocked = 0
    };
    strcpy(client.name, "Normal Client");
    
    // Simulate 1 second of normal activity
    for (int frame = 0; frame < 60; frame++) {
        // Send video frame (60 FPS)
        simulate_packet(&client, PACKET_VIDEO_FRAME, 2048);  // 2KB frames
        
        // Send audio every 4th frame (~15 audio packets/sec)
        if (frame % 4 == 0) {
            simulate_packet(&client, PACKET_AUDIO_BATCH, 4096);  // 4KB batch
        }
        
        // Send ping every 20th frame (3/sec)
        if (frame % 20 == 0) {
            simulate_packet(&client, PACKET_PING, 64);
        }
        
        usleep(16666);  // ~60 FPS timing (16.67ms)
    }
    
    printf("\nResults: %d sent, %d blocked\n", 
           client.packets_sent, client.packets_blocked);
    
    multi_rate_limiter_destroy(client.rate_limiter);
}

// Test 2: Abusive client trying to spam
void test_abusive_client(void) {
    printf("\n=== Test 2: Abusive Client (Spam Attack) ===\n");
    
    client_t client = {
        .client_id = 2,
        .rate_limiter = multi_rate_limiter_create(),
        .packets_sent = 0,
        .packets_blocked = 0
    };
    strcpy(client.name, "Abusive Client");
    
    // Try to send 1000 frames instantly
    printf("Attempting to send 1000 video frames instantly...\n");
    for (int i = 0; i < 1000; i++) {
        simulate_packet(&client, PACKET_VIDEO_FRAME, 2048);
    }
    
    printf("\nResults: %d sent, %d blocked (%.1f%% blocked)\n", 
           client.packets_sent, client.packets_blocked,
           100.0 * client.packets_blocked / (client.packets_sent + client.packets_blocked));
    
    multi_rate_limiter_destroy(client.rate_limiter);
}

// Test 3: Burst behavior
void test_burst_client(void) {
    printf("\n=== Test 3: Burst Client (Legitimate Burst) ===\n");
    
    client_t client = {
        .client_id = 3,
        .rate_limiter = multi_rate_limiter_create(),
        .packets_sent = 0,
        .packets_blocked = 0
    };
    strcpy(client.name, "Burst Client");
    
    printf("Sending burst of 120 frames (2 seconds worth)...\n");
    
    // Send burst of 120 frames (allowed by token bucket)
    for (int i = 0; i < 120; i++) {
        simulate_packet(&client, PACKET_VIDEO_FRAME, 2048);
    }
    
    printf("\nAfter burst: %d sent, %d blocked\n", 
           client.packets_sent, client.packets_blocked);
    
    // Wait for tokens to refill
    printf("\nWaiting 2 seconds for token refill...\n");
    sleep(2);
    
    // Try again
    client.packets_sent = 0;
    client.packets_blocked = 0;
    
    printf("\nTrying another 60 frames after wait...\n");
    for (int i = 0; i < 60; i++) {
        simulate_packet(&client, PACKET_VIDEO_FRAME, 2048);
    }
    
    printf("After refill: %d sent, %d blocked\n", 
           client.packets_sent, client.packets_blocked);
    
    multi_rate_limiter_destroy(client.rate_limiter);
}

// Test 4: Different rate limiter types
void test_different_types(void) {
    printf("\n=== Test 4: Comparing Rate Limiter Types ===\n");
    
    // Create three limiters with similar settings
    rate_limiter_t* token_bucket = rate_limiter_create_token_bucket("TokenBucket", 10.0, 20.0);
    rate_limiter_t* sliding_window = rate_limiter_create_sliding_window("SlidingWindow", 1, 10);
    rate_limiter_t* fixed_window = rate_limiter_create_fixed_window("FixedWindow", 10, 600);
    
    printf("\nSending 30 requests rapidly to each limiter...\n\n");
    
    int tb_allowed = 0, sw_allowed = 0, fw_allowed = 0;
    
    for (int i = 0; i < 30; i++) {
        if (rate_limiter_check(token_bucket, 100)) tb_allowed++;
        if (rate_limiter_check(sliding_window, 100)) sw_allowed++;
        if (rate_limiter_check(fixed_window, 100)) fw_allowed++;
    }
    
    printf("Token Bucket:    %d/30 allowed (burst handling)\n", tb_allowed);
    printf("Sliding Window:  %d/30 allowed (strict window)\n", sw_allowed);
    printf("Fixed Window:    %d/30 allowed (per-second limit)\n", fw_allowed);
    
    printf("\nWaiting 1 second...\n");
    sleep(1);
    
    printf("\nSending 10 more requests...\n");
    
    tb_allowed = 0; sw_allowed = 0; fw_allowed = 0;
    
    for (int i = 0; i < 10; i++) {
        if (rate_limiter_check(token_bucket, 100)) tb_allowed++;
        if (rate_limiter_check(sliding_window, 100)) sw_allowed++;
        if (rate_limiter_check(fixed_window, 100)) fw_allowed++;
    }
    
    printf("Token Bucket:    %d/10 allowed (refilled)\n", tb_allowed);
    printf("Sliding Window:  %d/10 allowed (window moved)\n", sw_allowed);
    printf("Fixed Window:    %d/10 allowed (new second)\n", fw_allowed);
    
    rate_limiter_destroy(token_bucket);
    rate_limiter_destroy(sliding_window);
    rate_limiter_destroy(fixed_window);
}

// Example: How to integrate into your server
void example_server_integration(void) {
    printf("\n=== Example: Server Integration ===\n");
    printf("Here's how you would integrate this into your server.c:\n\n");
    
    printf("```c\n");
    printf("// In client_info_t structure:\n");
    printf("typedef struct client_info {\n");
    printf("    // ... existing fields ...\n");
    printf("    multi_rate_limiter_t* rate_limiter;\n");
    printf("} client_info_t;\n\n");
    
    printf("// When client connects:\n");
    printf("client->rate_limiter = multi_rate_limiter_create();\n\n");
    
    printf("// In packet receive handler:\n");
    printf("switch (packet_type) {\n");
    printf("    case PACKET_TYPE_ASCII_FRAME:\n");
    printf("        if (!multi_rate_limiter_check_video(client->rate_limiter, len)) {\n");
    printf("            log_warn(\"Client %%u exceeded video rate limit\", client->client_id);\n");
    printf("            break; // Drop packet\n");
    printf("        }\n");
    printf("        // Process frame...\n");
    printf("        break;\n");
    printf("}\n\n");
    
    printf("// When client disconnects:\n");
    printf("multi_rate_limiter_destroy(client->rate_limiter);\n");
    printf("```\n");
}

// Test bandwidth limiting
void test_bandwidth_limiting(void) {
    printf("\n=== Test 5: Bandwidth Limiting ===\n");
    
    // Create a bandwidth limiter: 1MB/sec with 2MB burst
    rate_limit_config_t config = {
        .type = RATE_LIMIT_TOKEN_BUCKET,
        .max_requests_per_second = 1024.0,  // 1024 tokens/sec
        .burst_size = 2048.0,               // 2048 tokens burst
        .cost_per_byte = 1.0 / 1024.0       // 1 token per KB
    };
    
    rate_limiter_t* bandwidth_limiter = rate_limiter_create("Bandwidth", &config);
    
    printf("Bandwidth limit: 1MB/sec with 2MB burst capacity\n\n");
    
    // Try to send 3MB instantly
    int allowed = 0, blocked = 0;
    size_t chunk_size = 100 * 1024; // 100KB chunks
    
    printf("Attempting to send 3MB in 100KB chunks...\n");
    for (int i = 0; i < 30; i++) {
        if (rate_limiter_check(bandwidth_limiter, chunk_size)) {
            allowed++;
            printf(".");
        } else {
            blocked++;
            printf("X");
        }
        fflush(stdout);
    }
    
    printf("\n\nResults: %d chunks allowed (%.1f MB), %d blocked (%.1f MB)\n",
           allowed, allowed * 0.1, blocked, blocked * 0.1);
    
    printf("\nStatus: %s\n", rate_limiter_status(bandwidth_limiter));
    
    // Show refill behavior
    printf("\nWaiting 1 second for refill...\n");
    sleep(1);
    
    printf("Status after 1 sec: %s\n", rate_limiter_status(bandwidth_limiter));
    
    printf("\nTrying to send another 1MB...\n");
    allowed = 0; blocked = 0;
    for (int i = 0; i < 10; i++) {
        if (rate_limiter_check(bandwidth_limiter, chunk_size)) {
            allowed++;
            printf(".");
        } else {
            blocked++;
            printf("X");
        }
        fflush(stdout);
    }
    
    printf("\n\nResults: %d chunks allowed (%.1f MB), %d blocked (%.1f MB)\n",
           allowed, allowed * 0.1, blocked, blocked * 0.1);
    
    rate_limiter_destroy(bandwidth_limiter);
}

// Show statistics
void test_statistics(void) {
    printf("\n=== Test 6: Statistics Tracking ===\n");
    
    rate_limiter_t* limiter = rate_limiter_create_token_bucket("Stats", 10.0, 20.0);
    
    // Generate some traffic
    for (int second = 0; second < 3; second++) {
        printf("\nSecond %d:\n", second + 1);
        
        for (int i = 0; i < 15; i++) {
            bool allowed = rate_limiter_check(limiter, 1024);
            if (i % 5 == 0) {
                printf("  Request %2d: %s\n", i + 1, allowed ? "ALLOWED" : "BLOCKED");
            }
        }
        
        rate_limit_stats_t stats;
        rate_limiter_get_stats(limiter, &stats);
        
        printf("  Stats: %llu allowed, %llu blocked, %.2f avg rate, %llu bytes total\n",
               (unsigned long long)stats.allowed_count,
               (unsigned long long)stats.blocked_count,
               stats.avg_rate,
               (unsigned long long)stats.total_bytes);
        
        if (second < 2) {
            printf("  Sleeping 1 second...\n");
            sleep(1);
        }
    }
    
    rate_limiter_destroy(limiter);
}

int main(void) {
    printf("====================================\n");
    printf("    Rate Limiter Example Program    \n");
    printf("====================================\n");
    
    // Run all tests
    test_normal_client();
    test_abusive_client();
    test_burst_client();
    test_different_types();
    test_bandwidth_limiting();
    test_statistics();
    
    // Show integration example
    example_server_integration();
    
    printf("\n=== All Tests Complete ===\n");
    printf("\nKey Takeaways:\n");
    printf("1. Token Bucket is best for your use case (handles bursts)\n");
    printf("2. Set different limits per packet type\n");
    printf("3. Monitor both rate AND bandwidth\n");
    printf("4. Log violations before implementing blocks\n");
    printf("5. Start with generous limits and tighten based on data\n\n");
    
    return 0;
}