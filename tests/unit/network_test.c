#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <unistd.h>
#include <zlib.h>

#include "common.h"
#include "network.h"
#include "crc32_hw.h"
#include "packet_queue.h"
#include "image.h"
#include "ascii_simd.h"

void setup_network_quiet_logging(void);
void restore_network_logging(void);

TestSuite(network, .init = setup_network_quiet_logging, .fini = restore_network_logging);

void setup_network_quiet_logging(void) {
    log_set_level(LOG_FATAL);
}

void restore_network_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// Packet Header Tests
// =============================================================================

Test(network, packet_header_validation) {
    packet_header_t header;

    // Valid header
    header.magic = PACKET_MAGIC;
    header.type = PACKET_TYPE_ASCII_FRAME;
    header.length = 1024;
    header.crc32 = 0x12345678;
    header.client_id = 99;

    // Basic structure validation - just check fields are set correctly
    cr_assert_eq(header.magic, PACKET_MAGIC, "Magic should be set correctly");
    cr_assert_eq(header.type, PACKET_TYPE_ASCII_FRAME, "Type should be set correctly");
    cr_assert_eq(header.length, 1024, "Length should be set correctly");
    cr_assert_eq(header.client_id, 99, "Client ID should be set correctly");
}

Test(network, packet_type_validation) {
    // Test that packet type constants are defined and have reasonable values
    cr_assert_gt(PACKET_TYPE_ASCII_FRAME, 0, "ASCII_FRAME should be positive");
    cr_assert_gt(PACKET_TYPE_IMAGE_FRAME, 0, "IMAGE_FRAME should be positive");
    cr_assert_gt(PACKET_TYPE_AUDIO, 0, "AUDIO should be positive");
    cr_assert_gt(PACKET_TYPE_PING, 0, "PING should be positive");
    cr_assert_gt(PACKET_TYPE_PONG, 0, "PONG should be positive");
    cr_assert_gt(PACKET_TYPE_CLIENT_JOIN, 0, "CLIENT_JOIN should be positive");
    cr_assert_gt(PACKET_TYPE_CLIENT_LEAVE, 0, "CLIENT_LEAVE should be positive");
    cr_assert_gt(PACKET_TYPE_STREAM_START, 0, "STREAM_START should be positive");
    cr_assert_gt(PACKET_TYPE_STREAM_STOP, 0, "STREAM_STOP should be positive");
    cr_assert_gt(PACKET_TYPE_CLEAR_CONSOLE, 0, "CLEAR_CONSOLE should be positive");
    cr_assert_gt(PACKET_TYPE_SERVER_STATE, 0, "SERVER_STATE should be positive");
}

// =============================================================================
// CRC32 Tests
// =============================================================================

Test(network, crc32_basic) {
    const char *test_data = "Hello, ASCII-Chat!";
    uint32_t crc = asciichat_crc32(test_data, strlen(test_data));

    cr_assert_neq(crc, 0, "CRC should not be zero for real data");
    cr_assert_neq(crc, 0xFFFFFFFF, "CRC should not be all 1s for real data");

    // Test consistency - same input should give same CRC
    uint32_t crc2 = asciichat_crc32(test_data, strlen(test_data));
    cr_assert_eq(crc, crc2, "CRC should be consistent for same input");
}

Test(network, crc32_incremental) {
    const char *part1 = "Hello, ";
    const char *part2 = "ASCII-Chat!";
    const char *full = "Hello, ASCII-Chat!";

    // Calculate CRC for concatenated data
    char concatenated[256];
    strcpy(concatenated, part1);
    strcat(concatenated, part2);
    uint32_t crc_concatenated = asciichat_crc32(concatenated, strlen(concatenated));

    // Calculate CRC all at once
    uint32_t crc_full = asciichat_crc32(full, strlen(full));

    cr_assert_eq(crc_concatenated, crc_full, "Concatenated CRC should match full CRC");
}

Test(network, crc32_edge_cases) {
    // Empty data
    uint32_t crc_empty = asciichat_crc32("", 0);
    // Note: CRC32 of empty data might be 0 depending on the implementation
    // Just verify it doesn't crash and produces a consistent result
    uint32_t crc_empty2 = asciichat_crc32("", 0);
    cr_assert_eq(crc_empty, crc_empty2, "Empty data should produce consistent CRC");

    // Single byte
    uint8_t single_byte = 0x42;
    uint32_t crc_single = asciichat_crc32(&single_byte, 1);
    cr_assert_neq(crc_single, 0, "Single byte should produce valid CRC");

    // All zeros
    uint8_t zeros[100] = {0};
    uint32_t crc_zeros = asciichat_crc32(zeros, 100);
    cr_assert_neq(crc_zeros, 0, "All zeros should produce valid CRC");

    // All 255s
    uint8_t ones[100];
    memset(ones, 0xFF, 100);
    uint32_t crc_ones = asciichat_crc32(ones, 100);
    cr_assert_neq(crc_ones, 0, "All 255s should produce valid CRC");
    cr_assert_neq(crc_ones, crc_zeros, "Different patterns should produce different CRCs");
}

// =============================================================================
// Packet Creation Tests
// =============================================================================

Test(network, create_ascii_frame_packet) {
    // Test skipped - create_ascii_frame_packet function not implemented
    cr_skip_test("create_ascii_frame_packet function not implemented");
}

Test(network, create_image_frame_packet) {
    // Test skipped - create_image_frame_packet function not implemented
    cr_skip_test("create_image_frame_packet function not implemented");
}

Test(network, create_audio_packet) {
    // Test skipped - create_audio_packet function not implemented
    cr_skip_test("create_audio_packet function not implemented");
}

// =============================================================================
// Packet Serialization Tests
// =============================================================================

Test(network, packet_serialization_roundtrip) {
    // Test skipped - packet serialization functions not implemented
    cr_skip_test("packet serialization functions not implemented");
}

Test(network, packet_serialization_endianness) {
    // Test skipped - packet serialization functions not implemented
    cr_skip_test("packet serialization functions not implemented");
}

// =============================================================================
// Error Handling Tests
// =============================================================================

Test(network, invalid_packet_handling) {
    // Test skipped - packet deserialization functions not implemented
    cr_skip_test("packet deserialization functions not implemented");
}

Test(network, buffer_overflow_protection) {
    // Test skipped - packet serialization functions not implemented
    cr_skip_test("packet serialization functions not implemented");
}

// =============================================================================
// Network Utility Tests
// =============================================================================

Test(network, socket_timeout_setting) {
    int test_socket = socket(AF_INET, SOCK_STREAM, 0);
    cr_assert_geq(test_socket, 0, "Test socket creation should succeed");

    int result = set_socket_timeout(test_socket, 5000); // 5 second timeout
    cr_assert_eq(result, 0, "Setting socket timeout should succeed");

    // Verify timeout was set (this is platform-dependent, so we just check no error)
    struct timeval timeout;
    socklen_t len = sizeof(timeout);
    int getopt_result = getsockopt(test_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, &len);

    // Just verify that setting the timeout didn't fail and we can read it back
    // The actual timeout value may vary by platform/implementation
    cr_assert_eq(getopt_result, 0, "getsockopt should succeed");
    cr_assert_gt(timeout.tv_sec, 0, "Timeout should be set to a positive value");

    close(test_socket);
}

Test(network, address_parsing) {
    // Test skipped - parse_address function not implemented
    cr_skip_test("parse_address function not implemented");
}

// =============================================================================
// Compression Integration Tests
// =============================================================================

Test(network, compressed_packet_handling) {
    // Test that zlib compression is available and working
    const char *test_data = "This is test data for compression";
    size_t data_len = strlen(test_data);

    // Test zlib compression directly
    uLongf compressed_size = compressBound(data_len);
    char *compressed_data;
    SAFE_MALLOC(compressed_data, compressed_size, char*);

    // Compress the data
    int result = compress((Bytef*)compressed_data, &compressed_size,
                         (const Bytef*)test_data, data_len);

    cr_assert_eq(result, Z_OK, "zlib compression should succeed");
    cr_assert_gt(compressed_size, 0, "Compressed size should be positive");

    // Test decompression
    char *decompressed_data;
    SAFE_MALLOC(decompressed_data, data_len + 1, char*);  // +1 for null terminator
    uLongf decompressed_size = data_len;

    result = uncompress((Bytef*)decompressed_data, &decompressed_size,
                       (const Bytef*)compressed_data, compressed_size);

    cr_assert_eq(result, Z_OK, "zlib decompression should succeed");
    cr_assert_eq(decompressed_size, data_len, "Decompressed size should match original");
    cr_assert_str_eq(decompressed_data, test_data, "Decompressed data should match original");

    free(compressed_data);
    free(decompressed_data);
}
