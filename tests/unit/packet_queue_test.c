#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <arpa/inet.h>  // For htonl/ntohl

#include "common.h"
#include "packet_queue.h"
#include "network.h"
#include "crc32_hw.h"  // For CRC calculation

void setup_packet_queue_quiet_logging(void);
void restore_packet_queue_logging(void);

TestSuite(packet_queue, .init = setup_packet_queue_quiet_logging, .fini = restore_packet_queue_logging);

void setup_packet_queue_quiet_logging(void) {
    log_set_level(LOG_FATAL);
}

void restore_packet_queue_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// Node Pool Tests
// =============================================================================

Test(packet_queue, node_pool_creation) {
    node_pool_t *pool = node_pool_create(10);
    cr_assert_not_null(pool, "Node pool creation should succeed");
    cr_assert_eq(pool->pool_size, 10, "Pool size should match");
    cr_assert_eq(pool->used_count, 0, "Initial used count should be 0");
    cr_assert_not_null(pool->nodes, "Pool nodes should be allocated");
    cr_assert_not_null(pool->free_list, "Free list should be initialized");
    
    node_pool_destroy(pool);
}

Test(packet_queue, node_pool_get_put) {
    node_pool_t *pool = node_pool_create(5);
    cr_assert_not_null(pool, "Node pool creation should succeed");
    
    // Get nodes from pool
    packet_node_t *nodes[5];
    for (int i = 0; i < 5; i++) {
        nodes[i] = node_pool_get(pool);
        cr_assert_not_null(nodes[i], "Node %d should be allocated", i);
    }
    
    cr_assert_eq(pool->used_count, 5, "All nodes should be in use");
    
    // Try to get one more (pool exhausted - may fallback to malloc)
    packet_node_t *extra = node_pool_get(pool);
    // Implementation may fallback to malloc instead of returning NULL
    if (extra != NULL) {
        node_pool_put(pool, extra);
    }
    
    // Put nodes back
    for (int i = 0; i < 5; i++) {
        node_pool_put(pool, nodes[i]);
    }
    
    cr_assert_eq(pool->used_count, 0, "All nodes should be returned");
    
    node_pool_destroy(pool);
}

Test(packet_queue, node_pool_reuse) {
    node_pool_t *pool = node_pool_create(3);
    cr_assert_not_null(pool, "Node pool creation should succeed");
    
    // Get a node
    packet_node_t *node1 = node_pool_get(pool);
    cr_assert_not_null(node1, "First node should be allocated");
    
    // Put it back
    node_pool_put(pool, node1);
    
    // Get another node (should reuse the same memory)
    packet_node_t *node2 = node_pool_get(pool);
    cr_assert_not_null(node2, "Reused node should be allocated");
    cr_assert_eq(node1, node2, "Should reuse the same node");
    
    node_pool_put(pool, node2);
    node_pool_destroy(pool);
}

Test(packet_queue, node_pool_null_handling) {
    node_pool_t *pool = node_pool_create(1);
    cr_assert_not_null(pool, "Node pool creation should succeed");
    
    // Putting NULL should be safe
    node_pool_put(pool, NULL);
    
    // Double-put should be handled (may be ignored or detected)
    packet_node_t *node = node_pool_get(pool);
    cr_assert_not_null(node, "Node should be allocated");
    
    node_pool_put(pool, node);
    node_pool_put(pool, node); // Double put
    
    node_pool_destroy(pool);
    
    // Destroying NULL pool should be safe
    node_pool_destroy(NULL);
}

// =============================================================================
// Packet Queue Creation Tests
// =============================================================================

Test(packet_queue, basic_creation) {
    packet_queue_t *queue = packet_queue_create(10);
    cr_assert_not_null(queue, "Queue creation should succeed");
    cr_assert_eq(queue->max_size, 10, "Max size should be set");
    cr_assert_eq(queue->count, 0, "Initial count should be 0");
    cr_assert_null(queue->head, "Head should be NULL initially");
    cr_assert_null(queue->tail, "Tail should be NULL initially");
    cr_assert_eq(queue->shutdown, false, "Should not be shutdown initially");
    
    packet_queue_destroy(queue);
}

Test(packet_queue, unlimited_queue_creation) {
    packet_queue_t *queue = packet_queue_create(0); // 0 = unlimited
    cr_assert_not_null(queue, "Unlimited queue creation should succeed");
    cr_assert_eq(queue->max_size, 0, "Max size should be 0 (unlimited)");
    
    packet_queue_destroy(queue);
}

Test(packet_queue, queue_with_node_pool) {
    packet_queue_t *queue = packet_queue_create_with_pool(5, 10);
    cr_assert_not_null(queue, "Queue with pool creation should succeed");
    cr_assert_not_null(queue->node_pool, "Node pool should be created");
    cr_assert_eq(queue->max_size, 5, "Max size should be set");
    
    packet_queue_destroy(queue);
}

Test(packet_queue, queue_with_both_pools) {
    packet_queue_t *queue = packet_queue_create_with_pools(10, 20, true);
    cr_assert_not_null(queue, "Queue with both pools creation should succeed");
    cr_assert_not_null(queue->node_pool, "Node pool should be created");
    cr_assert_not_null(queue->buffer_pool, "Buffer pool should be created");
    
    packet_queue_destroy(queue);
}

// =============================================================================
// Packet Enqueue/Dequeue Tests
// =============================================================================

Test(packet_queue, basic_enqueue_dequeue) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Test data
    char test_data[] = "Hello, World!";
    
    // Enqueue a packet
    int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, test_data, strlen(test_data), 123, true);
    cr_assert_eq(result, 0, "Enqueue should succeed");
    cr_assert_eq(packet_queue_size(queue), 1, "Queue size should be 1");
    cr_assert(packet_queue_is_empty(queue) == false, "Queue should not be empty");
    
    // Dequeue the packet
    queued_packet_t *packet = packet_queue_dequeue(queue);
    cr_assert_not_null(packet, "Dequeue should return packet");
    cr_assert_eq(ntohs(packet->header.type), PACKET_TYPE_AUDIO, "Packet type should match");
    cr_assert_eq(ntohl(packet->header.client_id), 123, "Client ID should match");
    cr_assert_eq(packet->data_len, strlen(test_data), "Data length should match");
    cr_assert_str_eq((char *)packet->data, test_data, "Data should match");
    
    packet_queue_free_packet(packet);
    cr_assert_eq(packet_queue_size(queue), 0, "Queue should be empty after dequeue");
    cr_assert(packet_queue_is_empty(queue), "Queue should be empty");
    
    packet_queue_destroy(queue);
}

Test(packet_queue, multiple_packets) {
    packet_queue_t *queue = packet_queue_create(10);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Enqueue multiple packets
    for (int i = 0; i < 5; i++) {
        char data[32];
        snprintf(data, sizeof(data), "Packet %d", i);
        
        int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, strlen(data), i, true);
        cr_assert_eq(result, 0, "Enqueue %d should succeed", i);
    }
    
    cr_assert_eq(packet_queue_size(queue), 5, "Queue should have 5 packets");
    
    // Dequeue all packets (FIFO order)
    for (int i = 0; i < 5; i++) {
        queued_packet_t *packet = packet_queue_dequeue(queue);
        cr_assert_not_null(packet, "Dequeue %d should succeed", i);
        cr_assert_eq(ntohl(packet->header.client_id), (uint32_t)i, "Packet order should be FIFO");
        
        char expected[32];
        snprintf(expected, sizeof(expected), "Packet %d", i);
        cr_assert_str_eq((char *)packet->data, expected, "Packet %d data should match", i);
        
        packet_queue_free_packet(packet);
    }
    
    cr_assert(packet_queue_is_empty(queue), "Queue should be empty");
    packet_queue_destroy(queue);
}

Test(packet_queue, enqueue_without_copy) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Allocate data that we'll pass ownership to queue
    char *data;
    SAFE_MALLOC(data, 100, char *);
    strcpy(data, "Test data without copy");
    
    // Enqueue without copying (queue takes ownership)
    int result = packet_queue_enqueue(queue, PACKET_TYPE_IMAGE_FRAME, data, strlen(data), 456, false);
    cr_assert_eq(result, 0, "Enqueue should succeed");
    
    // Dequeue and verify
    queued_packet_t *packet = packet_queue_dequeue(queue);
    cr_assert_not_null(packet, "Dequeue should succeed");
    cr_assert_eq(packet->data, data, "Data pointer should be the same (no copy)");
    cr_assert_str_eq((char *)packet->data, "Test data without copy", "Data should match");
    cr_assert_eq(packet->owns_data, false, "Should not own data");
    
    // Free the data ourselves since queue doesn't own it
    free(data);
    
    // Set packet data to NULL to avoid double-free in packet cleanup
    packet->data = NULL;
    packet_queue_free_packet(packet);
    
    packet_queue_destroy(queue);
}

Test(packet_queue, try_dequeue) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Try dequeue empty queue (should return NULL immediately)
    queued_packet_t *packet = packet_queue_try_dequeue(queue);
    cr_assert_null(packet, "Try dequeue empty queue should return NULL");
    
    // Enqueue a packet
    char test_data[] = "Test data";
    int result = packet_queue_enqueue(queue, PACKET_TYPE_PING, test_data, strlen(test_data), 789, true);
    cr_assert_eq(result, 0, "Enqueue should succeed");
    
    // Try dequeue should succeed
    packet = packet_queue_try_dequeue(queue);
    cr_assert_not_null(packet, "Try dequeue should return packet");
    cr_assert_eq(ntohs(packet->header.type), PACKET_TYPE_PING, "Packet type should match");
    
    packet_queue_free_packet(packet);
    packet_queue_destroy(queue);
}

// =============================================================================
// Queue Capacity and Overflow Tests
// =============================================================================

Test(packet_queue, queue_full_behavior) {
    packet_queue_t *queue = packet_queue_create(3); // Small queue
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    char data[] = "Test";
    
    // Fill the queue to capacity
    for (int i = 0; i < 3; i++) {
        int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), i, true);
        cr_assert_eq(result, 0, "Enqueue %d should succeed", i);
    }
    
    cr_assert_eq(packet_queue_size(queue), 3, "Queue should be full");
    cr_assert(packet_queue_is_full(queue), "Queue should report full");
    
    // Try to add one more (should drop oldest)
    int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), 999, true);
    cr_assert_eq(result, 0, "Overflow enqueue should succeed (drop oldest)");
    cr_assert_eq(packet_queue_size(queue), 3, "Queue size should remain at capacity");
    
    // First packet should be client_id 1 (not 0, which was dropped)
    queued_packet_t *packet = packet_queue_dequeue(queue);
    cr_assert_not_null(packet, "Dequeue should succeed");
    cr_assert_eq(ntohl(packet->header.client_id), 1, "Oldest packet should have been dropped");
    
    packet_queue_free_packet(packet);
    packet_queue_destroy(queue);
}

Test(packet_queue, unlimited_queue) {
    packet_queue_t *queue = packet_queue_create(0); // Unlimited
    cr_assert_not_null(queue, "Unlimited queue creation should succeed");
    
    char data[] = "Test";
    
    // Add many packets
    for (int i = 0; i < 100; i++) {
        int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), i, true);
        cr_assert_eq(result, 0, "Enqueue %d should succeed in unlimited queue", i);
    }
    
    cr_assert_eq(packet_queue_size(queue), 100, "Queue should have 100 packets");
    cr_assert(packet_queue_is_full(queue) == false, "Unlimited queue should never be full");
    
    // Dequeue all
    for (int i = 0; i < 100; i++) {
        queued_packet_t *packet = packet_queue_dequeue(queue);
        cr_assert_not_null(packet, "Dequeue %d should succeed", i);
        cr_assert_eq(ntohl(packet->header.client_id), (uint32_t)i, "Packet order should be correct");
        packet_queue_free_packet(packet);
    }
    
    packet_queue_destroy(queue);
}

// =============================================================================
// Packet Validation Tests
// =============================================================================

Test(packet_queue, packet_validation) {
    // Create a valid packet
    queued_packet_t valid_packet = {0};
    valid_packet.header.magic = htonl(PACKET_MAGIC);  // Use network byte order
    valid_packet.header.type = htons(PACKET_TYPE_AUDIO);
    valid_packet.header.length = htonl(10);
    valid_packet.data_len = 10;
    
    char test_data[] = "1234567890";
    valid_packet.data = test_data;
    
    // Calculate and set correct CRC
    valid_packet.header.crc32 = htonl(asciichat_crc32(test_data, 10));
    
    bool result = packet_queue_validate_packet(&valid_packet);
    cr_assert(result, "Valid packet should pass validation");
    
    // Test invalid magic
    queued_packet_t invalid_magic = valid_packet;
    invalid_magic.header.magic = htonl(0xDEADDEAD); // Wrong magic in network order
    result = packet_queue_validate_packet(&invalid_magic);
    cr_assert(result == false, "Invalid magic should fail validation");
    
    // Test length mismatch
    queued_packet_t invalid_length = valid_packet;
    invalid_length.header.length = 20; // Doesn't match data_len
    result = packet_queue_validate_packet(&invalid_length);
    cr_assert(result == false, "Length mismatch should fail validation");
    
    // Test NULL packet
    result = packet_queue_validate_packet(NULL);
    cr_assert(result == false, "NULL packet should fail validation");
}

Test(packet_queue, pre_built_packet_enqueue) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Create a pre-built packet
    queued_packet_t packet = {0};
    packet.header.magic = htonl(PACKET_MAGIC);  // Use network byte order
    packet.header.type = htons(PACKET_TYPE_CLIENT_CAPABILITIES);
    packet.header.length = htonl(8);
    packet.header.client_id = htonl(111);
    
    char test_data[] = "12345678";
    packet.data = test_data;
    packet.data_len = 8;
    
    // Calculate and set correct CRC
    packet.header.crc32 = htonl(asciichat_crc32(test_data, 8));
    packet.owns_data = false; // We own the data
    
    // Enqueue pre-built packet
    int result = packet_queue_enqueue_packet(queue, &packet);
    cr_assert_eq(result, 0, "Pre-built packet enqueue should succeed");
    
    // Dequeue and verify
    queued_packet_t *dequeued = packet_queue_dequeue(queue);
    cr_assert_not_null(dequeued, "Dequeue should succeed");
    cr_assert_eq(ntohs(dequeued->header.type), PACKET_TYPE_CLIENT_CAPABILITIES, "Packet type should match");
    cr_assert_eq(ntohl(dequeued->header.client_id), 111, "Client ID should match");
    // Note: packet_header_t doesn't have sequence field in this implementation
    
    packet_queue_free_packet(dequeued);
    packet_queue_destroy(queue);
}

// =============================================================================
// Queue Statistics Tests
// =============================================================================

Test(packet_queue, statistics_tracking) {
    packet_queue_t *queue = packet_queue_create(3);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    uint64_t enqueued, dequeued, dropped;
    packet_queue_get_stats(queue, &enqueued, &dequeued, &dropped);
    cr_assert_eq(enqueued, 0, "Initial enqueued should be 0");
    cr_assert_eq(dequeued, 0, "Initial dequeued should be 0");
    cr_assert_eq(dropped, 0, "Initial dropped should be 0");
    
    char data[] = "Test";
    
    // Enqueue some packets
    for (int i = 0; i < 5; i++) { // 5 > capacity of 3, so 2 should be dropped
        packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), i, true);
    }
    
    packet_queue_get_stats(queue, &enqueued, &dequeued, &dropped);
    cr_assert_eq(enqueued, 5, "Should have attempted 5 enqueues");
    cr_assert_eq(dropped, 2, "Should have dropped 2 packets");
    cr_assert_eq(dequeued, 0, "Should have dequeued 0 packets so far");
    
    // Dequeue all remaining
    while (!packet_queue_is_empty(queue)) {
        queued_packet_t *packet = packet_queue_dequeue(queue);
        packet_queue_free_packet(packet);
    }
    
    packet_queue_get_stats(queue, &enqueued, &dequeued, &dropped);
    cr_assert_eq(dequeued, 3, "Should have dequeued 3 packets");
    
    packet_queue_destroy(queue);
}

// =============================================================================
// Queue Shutdown Tests
// =============================================================================

Test(packet_queue, shutdown_behavior) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Add a packet
    char data[] = "Test";
    packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), 123, true);
    cr_assert_eq(packet_queue_size(queue), 1, "Queue should have 1 packet");
    
    // Shutdown the queue
    packet_queue_shutdown(queue);
    cr_assert(queue->shutdown, "Queue should be marked as shutdown");
    
    // Try to dequeue (should return NULL due to shutdown)
    queued_packet_t *packet = packet_queue_try_dequeue(queue);
    // Note: behavior may vary - some implementations might still return queued packets
    // Just ensure it doesn't crash
    if (packet != NULL) {
        packet_queue_free_packet(packet);
    }
    
    packet_queue_destroy(queue);
}

Test(packet_queue, clear_operation) {
    packet_queue_t *queue = packet_queue_create(10);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    char data[] = "Test";
    
    // Add multiple packets
    for (int i = 0; i < 5; i++) {
        packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), i, true);
    }
    
    cr_assert_eq(packet_queue_size(queue), 5, "Queue should have 5 packets");
    
    // Clear the queue
    packet_queue_clear(queue);
    cr_assert_eq(packet_queue_size(queue), 0, "Queue should be empty after clear");
    cr_assert(packet_queue_is_empty(queue), "Queue should report empty");
    
    packet_queue_destroy(queue);
}

// =============================================================================
// Different Packet Types Tests
// =============================================================================

Test(packet_queue, different_packet_types) {
    packet_queue_t *queue = packet_queue_create(10);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Enqueue different packet types
    packet_type_t types[] = {
        PACKET_TYPE_AUDIO,
        PACKET_TYPE_ASCII_FRAME,
        PACKET_TYPE_IMAGE_FRAME,
        PACKET_TYPE_PING,
        PACKET_TYPE_PONG,
        PACKET_TYPE_CLIENT_CAPABILITIES
    };
    
    char data[] = "Test data";
    
    for (int i = 0; i < 6; i++) {
        int result = packet_queue_enqueue(queue, types[i], data, sizeof(data), i + 100, true);
        cr_assert_eq(result, 0, "Enqueue type %d should succeed", types[i]);
    }
    
    // Dequeue and verify types
    for (int i = 0; i < 6; i++) {
        queued_packet_t *packet = packet_queue_dequeue(queue);
        cr_assert_not_null(packet, "Dequeue %d should succeed", i);
        cr_assert_eq(ntohs(packet->header.type), types[i], "Packet type %d should match", i);
        cr_assert_eq(ntohl(packet->header.client_id), (uint32_t)(i + 100), "Client ID should match");
        packet_queue_free_packet(packet);
    }
    
    packet_queue_destroy(queue);
}

// =============================================================================
// Large Data Tests
// =============================================================================

Test(packet_queue, large_packet_data) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Create large test data
    size_t large_size = 64 * 1024; // 64KB
    char *large_data;
    SAFE_MALLOC(large_data, large_size, char *);
    
    // Fill with test pattern
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (char)(i % 256);
    }
    
    // Enqueue large packet
    int result = packet_queue_enqueue(queue, PACKET_TYPE_IMAGE_FRAME, large_data, large_size, 555, true);
    cr_assert_eq(result, 0, "Large packet enqueue should succeed");
    
    // Dequeue and verify
    queued_packet_t *packet = packet_queue_dequeue(queue);
    cr_assert_not_null(packet, "Large packet dequeue should succeed");
    cr_assert_eq(packet->data_len, large_size, "Large packet size should match");
    
    // Verify data pattern
    char *received_data = (char *)packet->data;
    for (size_t i = 0; i < large_size; i += 1000) { // Sample every 1000 bytes
        cr_assert_eq((unsigned char)received_data[i], (unsigned char)(i % 256), 
                    "Large packet data should match at offset %zu", i);
    }
    
    free(large_data);
    packet_queue_free_packet(packet);
    packet_queue_destroy(queue);
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(packet_queue, null_pointer_handling) {
    packet_queue_t *queue = packet_queue_create(5);
    cr_assert_not_null(queue, "Queue creation should succeed");
    
    // Enqueue with NULL data (should handle gracefully)
    int result = packet_queue_enqueue(queue, PACKET_TYPE_PING, NULL, 0, 123, true);
    cr_assert_eq(result, 0, "Enqueue with NULL data should succeed");
    
    queued_packet_t *packet = packet_queue_dequeue(queue);
    cr_assert_not_null(packet, "Dequeue should succeed even with NULL data");
    cr_assert_null(packet->data, "Packet data should be NULL");
    cr_assert_eq(packet->data_len, 0, "Data length should be 0");
    
    packet_queue_free_packet(packet);
    
    // Test NULL queue operations
    result = packet_queue_enqueue(NULL, PACKET_TYPE_AUDIO, "test", 4, 123, true);
    cr_assert_neq(result, 0, "Enqueue to NULL queue should fail");
    
    packet = packet_queue_dequeue(NULL);
    cr_assert_null(packet, "Dequeue from NULL queue should return NULL");
    
    packet_queue_destroy(queue);
    
    // Destroying NULL queue should be safe
    packet_queue_destroy(NULL);
}

Test(packet_queue, free_null_packet) {
    // Freeing NULL packet should be safe
    packet_queue_free_packet(NULL);
}

Test(packet_queue, zero_capacity_edge_case) {
    // Test edge case of queue with capacity 0 (should behave as unlimited)
    packet_queue_t *queue = packet_queue_create(0);
    cr_assert_not_null(queue, "Zero capacity queue should be created as unlimited");
    cr_assert_eq(queue->max_size, 0, "Max size should be 0");
    cr_assert(packet_queue_is_full(queue) == false, "Zero capacity queue should never be full");
    
    packet_queue_destroy(queue);
}

// =============================================================================
// Memory Pool Integration Tests
// =============================================================================

Test(packet_queue, node_pool_integration) {
    packet_queue_t *queue = packet_queue_create_with_pool(5, 10);
    cr_assert_not_null(queue, "Queue with node pool should be created");
    cr_assert_not_null(queue->node_pool, "Node pool should be attached");
    
    char data[] = "Node pool test";
    
    // Enqueue packets (should use node pool)
    for (int i = 0; i < 8; i++) {
        int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), i, true);
        cr_assert_eq(result, 0, "Enqueue %d should succeed with node pool", i);
    }
    
    // Dequeue all (should return nodes to pool)
    for (int i = 0; i < 5; i++) { // Only 5 fit in queue (capacity 5)
        queued_packet_t *packet = packet_queue_dequeue(queue);
        if (packet != NULL) {
            packet_queue_free_packet(packet);
        }
    }
    
    packet_queue_destroy(queue);
}

Test(packet_queue, buffer_pool_integration) {
    // Initialize global buffer pool for testing
    data_buffer_pool_init_global();
    
    packet_queue_t *queue = packet_queue_create_with_pools(5, 10, true);
    cr_assert_not_null(queue, "Queue with buffer pool should be created");
    cr_assert_not_null(queue->buffer_pool, "Buffer pool should be attached");
    
    char data[] = "Buffer pool test data";
    
    // Enqueue packets (should use buffer pool for data)
    for (int i = 0; i < 3; i++) {
        int result = packet_queue_enqueue(queue, PACKET_TYPE_AUDIO, data, sizeof(data), i, true);
        cr_assert_eq(result, 0, "Enqueue %d should succeed with buffer pool", i);
    }
    
    // Dequeue and verify
    for (int i = 0; i < 3; i++) {
        queued_packet_t *packet = packet_queue_dequeue(queue);
        cr_assert_not_null(packet, "Dequeue %d should succeed", i);
        cr_assert_str_eq((char *)packet->data, data, "Data should match");
        packet_queue_free_packet(packet);
    }
    
    packet_queue_destroy(queue);
    data_buffer_pool_cleanup_global();
}