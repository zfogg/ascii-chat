#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <arpa/inet.h>

#include "common.h"
#include "network.h"

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
    header.sequence = 42;
    header.crc32 = 0x12345678;
    header.client_id = 99;
    
    cr_assert(validate_packet_header(&header) == true, "Valid header should pass validation");
    
    // Invalid magic
    header.magic = 0xBADBAD;
    cr_assert(validate_packet_header(&header) == false, "Invalid magic should fail validation");
    
    // Reset and test invalid type
    header.magic = PACKET_MAGIC;
    header.type = 9999;
    cr_assert(validate_packet_header(&header) == false, "Invalid packet type should fail validation");
    
    // Reset and test oversized length
    header.type = PACKET_TYPE_ASCII_FRAME;
    header.length = MAX_PACKET_SIZE + 1;
    cr_assert(validate_packet_header(&header) == false, "Oversized packet should fail validation");
}

Test(network, packet_type_validation) {
    // Test all valid packet types
    cr_assert(validate_packet_type(PACKET_TYPE_ASCII_FRAME) == true, "ASCII_FRAME should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_IMAGE_FRAME) == true, "IMAGE_FRAME should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_AUDIO) == true, "AUDIO should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_SIZE) == true, "SIZE should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_PING) == true, "PING should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_PONG) == true, "PONG should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_CLIENT_JOIN) == true, "CLIENT_JOIN should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_CLIENT_LEAVE) == true, "CLIENT_LEAVE should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_STREAM_START) == true, "STREAM_START should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_STREAM_STOP) == true, "STREAM_STOP should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_CLEAR_CONSOLE) == true, "CLEAR_CONSOLE should be valid");
    cr_assert(validate_packet_type(PACKET_TYPE_SERVER_STATE) == true, "SERVER_STATE should be valid");
    
    // Test invalid packet types
    cr_assert(validate_packet_type(0) == false, "Type 0 should be invalid");
    cr_assert(validate_packet_type(999) == false, "Type 999 should be invalid");
    cr_assert(validate_packet_type(UINT16_MAX) == false, "Max uint16 should be invalid");
}

// =============================================================================
// CRC32 Tests
// =============================================================================

Test(network, crc32_basic) {
    const char *test_data = "Hello, ASCII-Chat!";
    uint32_t crc = crc32_calculate(0, (const uint8_t*)test_data, strlen(test_data));
    
    cr_assert_neq(crc, 0, "CRC should not be zero for real data");
    cr_assert_neq(crc, 0xFFFFFFFF, "CRC should not be all 1s for real data");
    
    // Test consistency - same input should give same CRC
    uint32_t crc2 = crc32_calculate(0, (const uint8_t*)test_data, strlen(test_data));
    cr_assert_eq(crc, crc2, "CRC should be consistent for same input");
}

Test(network, crc32_incremental) {
    const char *part1 = "Hello, ";
    const char *part2 = "ASCII-Chat!";
    const char *full = "Hello, ASCII-Chat!";
    
    // Calculate CRC incrementally
    uint32_t crc_incremental = crc32_calculate(0, (const uint8_t*)part1, strlen(part1));
    crc_incremental = crc32_calculate(crc_incremental, (const uint8_t*)part2, strlen(part2));
    
    // Calculate CRC all at once
    uint32_t crc_full = crc32_calculate(0, (const uint8_t*)full, strlen(full));
    
    cr_assert_eq(crc_incremental, crc_full, "Incremental CRC should match full CRC");
}

Test(network, crc32_edge_cases) {
    // Empty data
    uint32_t crc_empty = crc32_calculate(0, NULL, 0);
    cr_assert_neq(crc_empty, 0, "Empty data should still produce valid CRC");
    
    // Single byte
    uint8_t single_byte = 0x42;
    uint32_t crc_single = crc32_calculate(0, &single_byte, 1);
    cr_assert_neq(crc_single, 0, "Single byte should produce valid CRC");
    
    // All zeros
    uint8_t zeros[100] = {0};
    uint32_t crc_zeros = crc32_calculate(0, zeros, 100);
    cr_assert_neq(crc_zeros, 0, "All zeros should produce valid CRC");
    
    // All 255s
    uint8_t ones[100];
    memset(ones, 0xFF, 100);
    uint32_t crc_ones = crc32_calculate(0, ones, 100);
    cr_assert_neq(crc_ones, 0, "All 255s should produce valid CRC");
    cr_assert_neq(crc_ones, crc_zeros, "Different patterns should produce different CRCs");
}

// =============================================================================
// Packet Creation Tests
// =============================================================================

Test(network, create_ascii_frame_packet) {
    const char *ascii_data = "████████\n████████\n";
    packet_t packet;
    
    int result = create_ascii_frame_packet(&packet, ascii_data, strlen(ascii_data), 123);
    cr_assert_eq(result, 0, "ASCII frame packet creation should succeed");
    
    cr_assert_eq(packet.header.magic, PACKET_MAGIC, "Magic should be set correctly");
    cr_assert_eq(packet.header.type, PACKET_TYPE_ASCII_FRAME, "Type should be ASCII_FRAME");
    cr_assert_eq(packet.header.length, strlen(ascii_data), "Length should match data size");
    cr_assert_eq(packet.header.client_id, 123, "Client ID should be set correctly");
    
    cr_assert_not_null(packet.data, "Packet data should not be null");
    cr_assert_str_eq((char*)packet.data, ascii_data, "Packet data should match input");
    
    free(packet.data);
}

Test(network, create_image_frame_packet) {
    // Create test RGB image data
    const size_t width = 32, height = 24;
    rgb_pixel_t *image_data;
    SAFE_MALLOC(image_data, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    // Fill with test pattern
    for (size_t i = 0; i < width * height; i++) {
        image_data[i] = (rgb_pixel_t){
            .r = (i % 256),
            .g = ((i * 2) % 256),
            .b = ((i * 3) % 256)
        };
    }
    
    packet_t packet;
    int result = create_image_frame_packet(&packet, image_data, width, height, 456);
    cr_assert_eq(result, 0, "Image frame packet creation should succeed");
    
    cr_assert_eq(packet.header.magic, PACKET_MAGIC, "Magic should be set correctly");
    cr_assert_eq(packet.header.type, PACKET_TYPE_IMAGE_FRAME, "Type should be IMAGE_FRAME");
    cr_assert_eq(packet.header.client_id, 456, "Client ID should be set correctly");
    
    // Verify the image data structure
    image_frame_data_t *frame_data = (image_frame_data_t*)packet.data;
    cr_assert_eq(frame_data->width, width, "Width should match");
    cr_assert_eq(frame_data->height, height, "Height should match");
    
    // Verify pixel data
    rgb_pixel_t *received_pixels = (rgb_pixel_t*)(frame_data + 1);
    for (size_t i = 0; i < width * height; i++) {
        cr_assert_eq(received_pixels[i].r, image_data[i].r, "Red channel should match at pixel %zu", i);
        cr_assert_eq(received_pixels[i].g, image_data[i].g, "Green channel should match at pixel %zu", i);
        cr_assert_eq(received_pixels[i].b, image_data[i].b, "Blue channel should match at pixel %zu", i);
    }
    
    free(image_data);
    free(packet.data);
}

Test(network, create_audio_packet) {
    // Create test audio samples
    const size_t sample_count = 1024;
    float *audio_samples;
    SAFE_MALLOC(audio_samples, sample_count * sizeof(float), float*);
    
    // Fill with sine wave
    for (size_t i = 0; i < sample_count; i++) {
        audio_samples[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
    }
    
    packet_t packet;
    int result = create_audio_packet(&packet, audio_samples, sample_count, 789);
    cr_assert_eq(result, 0, "Audio packet creation should succeed");
    
    cr_assert_eq(packet.header.magic, PACKET_MAGIC, "Magic should be set correctly");
    cr_assert_eq(packet.header.type, PACKET_TYPE_AUDIO, "Type should be AUDIO");
    cr_assert_eq(packet.header.client_id, 789, "Client ID should be set correctly");
    
    // Verify audio data
    float *received_samples = (float*)packet.data;
    for (size_t i = 0; i < sample_count; i++) {
        cr_assert_float_eq(received_samples[i], audio_samples[i], 0.0001f, 
                          "Audio sample should match at index %zu", i);
    }
    
    free(audio_samples);
    free(packet.data);
}

// =============================================================================
// Packet Serialization Tests
// =============================================================================

Test(network, packet_serialization_roundtrip) {
    const char *test_data = "Test packet data for roundtrip";
    packet_t original_packet;
    
    int result = create_ascii_frame_packet(&original_packet, test_data, strlen(test_data), 999);
    cr_assert_eq(result, 0, "Original packet creation should succeed");
    
    // Serialize packet
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t serialized_size = serialize_packet(&original_packet, buffer, sizeof(buffer));
    cr_assert_gt(serialized_size, sizeof(packet_header_t), "Serialized size should include header and data");
    
    // Deserialize packet
    packet_t deserialized_packet;
    result = deserialize_packet(buffer, serialized_size, &deserialized_packet);
    cr_assert_eq(result, 0, "Deserialization should succeed");
    
    // Verify header fields
    cr_assert_eq(deserialized_packet.header.magic, original_packet.header.magic, "Magic should match");
    cr_assert_eq(deserialized_packet.header.type, original_packet.header.type, "Type should match");
    cr_assert_eq(deserialized_packet.header.length, original_packet.header.length, "Length should match");
    cr_assert_eq(deserialized_packet.header.sequence, original_packet.header.sequence, "Sequence should match");
    cr_assert_eq(deserialized_packet.header.client_id, original_packet.header.client_id, "Client ID should match");
    
    // Verify data
    cr_assert_not_null(deserialized_packet.data, "Deserialized data should not be null");
    cr_assert_str_eq((char*)deserialized_packet.data, test_data, "Data should match original");
    
    // Cleanup
    free(original_packet.data);
    free(deserialized_packet.data);
}

Test(network, packet_serialization_endianness) {
    packet_t packet;
    const char *test_data = "Endianness test";
    
    create_ascii_frame_packet(&packet, test_data, strlen(test_data), 1);
    
    // Set specific values that would be affected by endianness
    packet.header.sequence = 0x12345678;
    packet.header.length = 0xABCDEF12;
    
    // Serialize
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t size = serialize_packet(&packet, buffer, sizeof(buffer));
    
    // Check that header values are in network byte order
    packet_header_t *serialized_header = (packet_header_t*)buffer;
    cr_assert_eq(ntohl(serialized_header->magic), PACKET_MAGIC, "Magic should be in network byte order");
    cr_assert_eq(ntohl(serialized_header->sequence), 0x12345678, "Sequence should be in network byte order");
    cr_assert_eq(ntohl(serialized_header->length), strlen(test_data), "Length should be in network byte order");
    
    // Deserialize and verify conversion back
    packet_t deserialized;
    deserialize_packet(buffer, size, &deserialized);
    cr_assert_eq(deserialized.header.sequence, 0x12345678, "Sequence should be converted back to host order");
    
    free(packet.data);
    free(deserialized.data);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

Test(network, invalid_packet_handling) {
    uint8_t corrupt_data[100];
    packet_t packet;
    
    // Fill with garbage data
    for (int i = 0; i < 100; i++) {
        corrupt_data[i] = rand() % 256;
    }
    
    int result = deserialize_packet(corrupt_data, 100, &packet);
    cr_assert_neq(result, 0, "Deserializing corrupt data should fail");
    
    // Test with undersized packet
    result = deserialize_packet(corrupt_data, sizeof(packet_header_t) - 1, &packet);
    cr_assert_neq(result, 0, "Undersized packet should fail deserialization");
    
    // Test with oversized claimed length
    packet_header_t *header = (packet_header_t*)corrupt_data;
    header->magic = htonl(PACKET_MAGIC);
    header->type = htons(PACKET_TYPE_ASCII_FRAME);
    header->length = htonl(MAX_PACKET_SIZE + 1);
    
    result = deserialize_packet(corrupt_data, 100, &packet);
    cr_assert_neq(result, 0, "Oversized packet should fail deserialization");
}

Test(network, buffer_overflow_protection) {
    // Test serialization with insufficient buffer
    packet_t packet;
    const char *large_data = "Very large data that should not fit in small buffer";
    
    create_ascii_frame_packet(&packet, large_data, strlen(large_data), 1);
    
    uint8_t small_buffer[10]; // Intentionally too small
    size_t result = serialize_packet(&packet, small_buffer, sizeof(small_buffer));
    cr_assert_eq(result, 0, "Serialization should fail gracefully with insufficient buffer");
    
    free(packet.data);
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
    getsockopt(test_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, &len);
    
    // Timeout should be approximately 5 seconds (allow some tolerance)
    int timeout_ms = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
    cr_assert_geq(timeout_ms, 4900, "Timeout should be at least 4.9 seconds");
    cr_assert_leq(timeout_ms, 5100, "Timeout should be at most 5.1 seconds");
    
    close(test_socket);
}

Test(network, address_parsing) {
    // Test valid IPv4 addresses
    struct sockaddr_in addr;
    int result;
    
    result = parse_address("127.0.0.1", 8080, &addr);
    cr_assert_eq(result, 0, "Valid IPv4 address should parse successfully");
    cr_assert_eq(addr.sin_family, AF_INET, "Address family should be AF_INET");
    cr_assert_eq(ntohs(addr.sin_port), 8080, "Port should be set correctly");
    
    result = parse_address("0.0.0.0", 9999, &addr);
    cr_assert_eq(result, 0, "0.0.0.0 should parse successfully");
    
    result = parse_address("255.255.255.255", 1, &addr);
    cr_assert_eq(result, 0, "255.255.255.255 should parse successfully");
    
    // Test invalid addresses
    result = parse_address("256.1.1.1", 8080, &addr);
    cr_assert_neq(result, 0, "Invalid IPv4 address should fail to parse");
    
    result = parse_address("not.an.ip.address", 8080, &addr);
    cr_assert_neq(result, 0, "Non-numeric address should fail to parse");
    
    result = parse_address("127.0.0.1", 0, &addr);
    cr_assert_neq(result, 0, "Port 0 should be invalid");
    
    result = parse_address("127.0.0.1", 65536, &addr);
    cr_assert_neq(result, 0, "Port > 65535 should be invalid");
}

// =============================================================================
// Compression Integration Tests
// =============================================================================

Test(network, compressed_packet_handling) {
    // Create large ASCII data that should benefit from compression
    char large_ascii[5000];
    memset(large_ascii, '█', sizeof(large_ascii) - 1);
    large_ascii[sizeof(large_ascii) - 1] = '\0';
    
    packet_t packet;
    int result = create_ascii_frame_packet(&packet, large_ascii, strlen(large_ascii), 1);
    cr_assert_eq(result, 0, "Large ASCII packet creation should succeed");
    
    // Test that compression actually reduces size for repetitive data
    uint8_t compressed_buffer[MAX_PACKET_SIZE];
    size_t original_size = packet.header.length;
    
    size_t compressed_size = compress_packet_data(packet.data, original_size, 
                                                  compressed_buffer, sizeof(compressed_buffer));
    
    cr_assert_gt(compressed_size, 0, "Compression should succeed");
    cr_assert_lt(compressed_size, original_size, "Compressed size should be smaller than original");
    
    // Test decompression
    uint8_t decompressed_buffer[MAX_PACKET_SIZE];
    size_t decompressed_size = decompress_packet_data(compressed_buffer, compressed_size,
                                                      decompressed_buffer, sizeof(decompressed_buffer));
    
    cr_assert_eq(decompressed_size, original_size, "Decompressed size should match original");
    cr_assert(memcmp(packet.data, decompressed_buffer, original_size) == 0, 
              "Decompressed data should match original");
    
    free(packet.data);
}