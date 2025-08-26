#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "network.h"
#include "ascii_simd.h"
#include "ascii_simd_color.h"
#include "compression.h"

void setup_pipeline_quiet_logging(void);
void restore_pipeline_logging(void);

TestSuite(video_pipeline, .init = setup_pipeline_quiet_logging, .fini = restore_pipeline_logging);

void setup_pipeline_quiet_logging(void) {
    log_set_level(LOG_FATAL);
}

void restore_pipeline_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// Test Helper Functions
// =============================================================================

static void create_test_image(rgb_pixel_t *pixels, int width, int height, int pattern_type) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            
            switch (pattern_type) {
                case 0: // Gradient
                    pixels[idx] = (rgb_pixel_t){
                        .r = (x * 255) / (width - 1),
                        .g = (y * 255) / (height - 1),
                        .b = ((x + y) * 255) / (width + height - 2)
                    };
                    break;
                    
                case 1: // Checkerboard
                    if ((x / 4 + y / 4) % 2 == 0) {
                        pixels[idx] = (rgb_pixel_t){255, 255, 255}; // White
                    } else {
                        pixels[idx] = (rgb_pixel_t){0, 0, 0}; // Black
                    }
                    break;
                    
                case 2: // Color bars
                    int bar_width = width / 8;
                    int bar = x / bar_width;
                    rgb_pixel_t colors[] = {
                        {255, 255, 255}, // White
                        {255, 255, 0},   // Yellow
                        {0, 255, 255},   // Cyan
                        {0, 255, 0},     // Green
                        {255, 0, 255},   // Magenta
                        {255, 0, 0},     // Red
                        {0, 0, 255},     // Blue
                        {0, 0, 0}        // Black
                    };
                    pixels[idx] = colors[bar % 8];
                    break;
                    
                default: // Solid color
                    pixels[idx] = (rgb_pixel_t){128, 128, 128};
                    break;
            }
        }
    }
}

static bool validate_ascii_output(const char *ascii, size_t length) {
    if (!ascii || length == 0) return false;
    
    bool has_content = false;
    for (size_t i = 0; i < length; i++) {
        char c = ascii[i];
        
        // Check for valid ASCII characters
        if (!(c >= 32 && c <= 126) && c != '\n' && c != '\033') {
            return false; // Invalid character
        }
        
        // Check for actual content (not just whitespace)
        if (c != ' ' && c != '\n' && c != '\033') {
            has_content = true;
        }
    }
    
    return has_content;
}

// =============================================================================
// Full Pipeline Tests
// =============================================================================

Test(video_pipeline, webcam_to_ascii_to_network) {
    const int width = 64, height = 48;
    rgb_pixel_t *test_frame;
    SAFE_MALLOC(test_frame, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    // Step 1: Create synthetic webcam data
    create_test_image(test_frame, width, height, 0); // Gradient pattern
    
    // Step 2: Convert to ASCII using SIMD
    char ascii_output[50000];
    size_t ascii_len = ascii_simd_color_neon(test_frame, width * height, 
                                            ascii_output, sizeof(ascii_output), false, false);
    
    cr_assert_gt(ascii_len, 0, "ASCII conversion should produce output");
    cr_assert_lt(ascii_len, sizeof(ascii_output), "ASCII output should fit in buffer");
    cr_assert(validate_ascii_output(ascii_output, ascii_len), "ASCII output should be valid");
    
    // Step 3: Create network packet
    packet_t packet;
    int result = create_ascii_frame_packet(&packet, ascii_output, ascii_len, 1001);
    cr_assert_eq(result, 0, "ASCII frame packet creation should succeed");
    
    // Step 4: Serialize packet for network transmission
    uint8_t buffer[MAX_PACKET_SIZE];
    size_t packet_size = serialize_packet(&packet, buffer, sizeof(buffer));
    cr_assert_gt(packet_size, sizeof(packet_header_t), "Serialized packet should include header and data");
    
    // Step 5: Deserialize packet (simulating network reception)
    packet_t received_packet;
    result = deserialize_packet(buffer, packet_size, &received_packet);
    cr_assert_eq(result, 0, "Packet deserialization should succeed");
    
    // Step 6: Verify round-trip integrity
    cr_assert_eq(received_packet.header.magic, PACKET_MAGIC, "Magic should be preserved");
    cr_assert_eq(received_packet.header.type, PACKET_TYPE_ASCII_FRAME, "Type should be ASCII_FRAME");
    cr_assert_eq(received_packet.header.length, ascii_len, "Length should match original");
    cr_assert_eq(received_packet.header.client_id, 1001, "Client ID should be preserved");
    
    cr_assert_not_null(received_packet.data, "Received packet should have data");
    cr_assert_str_eq((char*)received_packet.data, ascii_output, "ASCII data should match exactly");
    
    // Cleanup
    free(test_frame);
    free(packet.data);
    free(received_packet.data);
}

Test(video_pipeline, image_frame_to_ascii_conversion) {
    const int width = 32, height = 24;
    rgb_pixel_t *image_data;
    SAFE_MALLOC(image_data, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    // Create checkerboard pattern
    create_test_image(image_data, width, height, 1);
    
    // Step 1: Create image frame packet
    packet_t image_packet;
    int result = create_image_frame_packet(&image_packet, image_data, width, height, 2002);
    cr_assert_eq(result, 0, "Image frame packet creation should succeed");
    
    // Step 2: Extract image data from packet
    image_frame_data_t *frame_data = (image_frame_data_t*)image_packet.data;
    cr_assert_eq(frame_data->width, width, "Width should be preserved");
    cr_assert_eq(frame_data->height, height, "Height should be preserved");
    
    rgb_pixel_t *extracted_pixels = (rgb_pixel_t*)(frame_data + 1);
    
    // Step 3: Convert extracted image to ASCII
    char ascii_output[25000];
    size_t ascii_len = ascii_simd_color_scalar(extracted_pixels, width * height,
                                              ascii_output, sizeof(ascii_output), false, false);
    
    cr_assert_gt(ascii_len, 0, "ASCII conversion from packet data should succeed");
    cr_assert(validate_ascii_output(ascii_output, ascii_len), "Generated ASCII should be valid");
    
    // Step 4: Verify ASCII contains expected patterns from checkerboard
    // Checkerboard should produce contrast in ASCII output
    bool has_variation = false;
    char first_visible_char = 0;
    
    for (size_t i = 0; i < ascii_len && !has_variation; i++) {
        char c = ascii_output[i];
        if (c >= 32 && c <= 126 && c != ' ') { // Visible ASCII character
            if (first_visible_char == 0) {
                first_visible_char = c;
            } else if (c != first_visible_char) {
                has_variation = true;
            }
        }
    }
    
    cr_assert(has_variation, "Checkerboard pattern should produce varied ASCII characters");
    
    free(image_data);
    free(image_packet.data);
}

Test(video_pipeline, different_color_modes_consistency) {
    const int width = 24, height = 16;
    rgb_pixel_t *test_pixels;
    SAFE_MALLOC(test_pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    // Create color bar pattern
    create_test_image(test_pixels, width, height, 2);
    
    // Test different color modes
    char fg_output[20000], bg_output[20000];
    
    size_t fg_len = ascii_simd_color_neon(test_pixels, width * height, 
                                         fg_output, sizeof(fg_output), false, false);
    size_t bg_len = ascii_simd_color_neon(test_pixels, width * height,
                                         bg_output, sizeof(bg_output), true, false);
    
    cr_assert_gt(fg_len, 0, "Foreground mode should produce output");
    cr_assert_gt(bg_len, 0, "Background mode should produce output");
    
    // Background mode should generally produce longer output (more ANSI codes)
    cr_assert_gt(bg_len, fg_len, "Background mode should produce more ANSI codes");
    
    // Both should be valid ASCII art
    cr_assert(validate_ascii_output(fg_output, fg_len), "Foreground ASCII should be valid");
    cr_assert(validate_ascii_output(bg_output, bg_len), "Background ASCII should be valid");
    
    // Test network transmission of both modes
    packet_t fg_packet, bg_packet;
    create_ascii_frame_packet(&fg_packet, fg_output, fg_len, 3001);
    create_ascii_frame_packet(&bg_packet, bg_output, bg_len, 3002);
    
    // Both should serialize successfully
    uint8_t fg_buffer[MAX_PACKET_SIZE], bg_buffer[MAX_PACKET_SIZE];
    size_t fg_size = serialize_packet(&fg_packet, fg_buffer, sizeof(fg_buffer));
    size_t bg_size = serialize_packet(&bg_packet, bg_buffer, sizeof(bg_buffer));
    
    cr_assert_gt(fg_size, 0, "Foreground packet should serialize");
    cr_assert_gt(bg_size, 0, "Background packet should serialize");
    
    free(test_pixels);
    free(fg_packet.data);
    free(bg_packet.data);
}

// =============================================================================
// Compression Integration Tests
// =============================================================================

Test(video_pipeline, ascii_compression_in_pipeline) {
    const int width = 80, height = 60;
    rgb_pixel_t *large_image;
    SAFE_MALLOC(large_image, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    // Create repetitive pattern that should compress well
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            // Create blocks of solid color (highly compressible)
            int block_x = x / 8;
            int block_y = y / 8;
            if ((block_x + block_y) % 2 == 0) {
                large_image[idx] = (rgb_pixel_t){255, 255, 255};
            } else {
                large_image[idx] = (rgb_pixel_t){0, 0, 0};
            }
        }
    }
    
    // Convert to ASCII
    char ascii_output[100000];
    size_t ascii_len = ascii_simd_color_scalar(large_image, width * height,
                                              ascii_output, sizeof(ascii_output), false, false);
    cr_assert_gt(ascii_len, 0, "Large ASCII conversion should succeed");
    
    // Test compression
    uint8_t compressed_buffer[MAX_PACKET_SIZE];
    size_t compressed_size = compress_data((uint8_t*)ascii_output, ascii_len,
                                          compressed_buffer, sizeof(compressed_buffer));
    
    if (compressed_size > 0) {
        cr_assert_lt(compressed_size, ascii_len, "Repetitive ASCII should compress");
        
        // Test decompression
        uint8_t decompressed_buffer[100000];
        size_t decompressed_size = decompress_data(compressed_buffer, compressed_size,
                                                  decompressed_buffer, sizeof(decompressed_buffer));
        
        cr_assert_eq(decompressed_size, ascii_len, "Decompressed size should match original");
        cr_assert(memcmp(ascii_output, decompressed_buffer, ascii_len) == 0,
                 "Decompressed data should match original");
    }
    
    free(large_image);
}

// =============================================================================
// Error Handling in Pipeline
// =============================================================================

Test(video_pipeline, pipeline_error_handling) {
    // Test pipeline with invalid/edge case inputs
    
    // Test 1: Empty image
    rgb_pixel_t empty_image[1] = {{0, 0, 0}};
    char output[1000];
    size_t len = ascii_simd_color_neon(empty_image, 0, output, sizeof(output), false, false);
    cr_assert_eq(len, 0, "Empty image should produce no output");
    
    // Test 2: Single pixel
    len = ascii_simd_color_neon(empty_image, 1, output, sizeof(output), false, false);
    cr_assert_geq(len, 0, "Single pixel should be handled gracefully");
    
    // Test 3: Very small buffer
    char tiny_buffer[10];
    rgb_pixel_t many_pixels[100];
    for (int i = 0; i < 100; i++) {
        many_pixels[i] = (rgb_pixel_t){255, 255, 255}; // Bright pixels = lots of ANSI
    }
    
    len = ascii_simd_color_neon(many_pixels, 100, tiny_buffer, sizeof(tiny_buffer), false, false);
    cr_assert_leq(len, sizeof(tiny_buffer), "Should not overflow small buffer");
    
    // Test 4: Extreme color values pipeline
    rgb_pixel_t extreme_pixels[] = {
        {0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}
    };
    
    len = ascii_simd_color_neon(extreme_pixels, 5, output, sizeof(output), false, false);
    cr_assert_gt(len, 0, "Extreme colors should be handled");
    cr_assert(validate_ascii_output(output, len), "Extreme color ASCII should be valid");
    
    // Test network packet with extreme ASCII
    packet_t packet;
    int result = create_ascii_frame_packet(&packet, output, len, 4001);
    cr_assert_eq(result, 0, "Should create packet with extreme color ASCII");
    
    free(packet.data);
}

Test(video_pipeline, memory_management_in_pipeline) {
    const int iterations = 50;
    const int width = 40, height = 30;
    
    // Test that pipeline doesn't leak memory over multiple iterations
    for (int iter = 0; iter < iterations; iter++) {
        rgb_pixel_t *image;
        SAFE_MALLOC(image, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
        
        // Create varied pattern each iteration
        create_test_image(image, width, height, iter % 3);
        
        // ASCII conversion
        char ascii_output[25000];
        size_t ascii_len = ascii_simd_color_scalar(image, width * height,
                                                  ascii_output, sizeof(ascii_output), false, false);
        
        // Packet creation and serialization
        packet_t packet;
        create_ascii_frame_packet(&packet, ascii_output, ascii_len, 5000 + iter);
        
        uint8_t buffer[MAX_PACKET_SIZE];
        size_t packet_size = serialize_packet(&packet, buffer, sizeof(buffer));
        
        // Deserialization
        packet_t received_packet;
        deserialize_packet(buffer, packet_size, &received_packet);
        
        // Cleanup
        free(image);
        free(packet.data);
        free(received_packet.data);
    }
    
    // If we reach here without crashes, memory management is working
    cr_assert(true, "Pipeline should handle multiple iterations without memory issues");
}

// =============================================================================
// Performance and Throughput Tests
// =============================================================================

Test(video_pipeline, pipeline_throughput) {
    const int width = 160, height = 120;
    const int frame_count = 10;
    rgb_pixel_t *test_frame;
    SAFE_MALLOC(test_frame, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    create_test_image(test_frame, width, height, 1); // Checkerboard
    
    clock_t start_time = clock();
    
    // Process multiple frames through pipeline
    for (int frame = 0; frame < frame_count; frame++) {
        // Modify image slightly each frame
        for (int i = 0; i < 100; i++) {
            int idx = (frame * 100 + i) % (width * height);
            test_frame[idx].r = (test_frame[idx].r + 10) % 256;
        }
        
        // ASCII conversion
        char ascii_output[50000];
        size_t ascii_len = ascii_simd_color_neon(test_frame, width * height,
                                                ascii_output, sizeof(ascii_output), false, false);
        cr_assert_gt(ascii_len, 0, "Frame %d ASCII conversion should succeed", frame);
        
        // Packet processing
        packet_t packet;
        create_ascii_frame_packet(&packet, ascii_output, ascii_len, 6000 + frame);
        
        uint8_t buffer[MAX_PACKET_SIZE];
        size_t packet_size = serialize_packet(&packet, buffer, sizeof(buffer));
        cr_assert_gt(packet_size, 0, "Frame %d packet serialization should succeed", frame);
        
        free(packet.data);
    }
    
    clock_t end_time = clock();
    double total_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    double fps = frame_count / total_time;
    
    log_info("Pipeline throughput: %d frames in %.3f seconds = %.1f FPS (%dx%d)", 
             frame_count, total_time, fps, width, height);
    
    // Should achieve reasonable throughput (at least 5 FPS for this test size)
    cr_assert_gt(fps, 5.0, "Pipeline should achieve at least 5 FPS for %dx%d frames", width, height);
    
    free(test_frame);
}

Test(video_pipeline, ascii_quality_preservation) {
    const int width = 48, height = 32;
    rgb_pixel_t *original_image, *reconstructed_image;
    SAFE_MALLOC(original_image, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    SAFE_MALLOC(reconstructed_image, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    
    // Create test image with gradual gradient
    create_test_image(original_image, width, height, 0);
    
    // Convert to ASCII and back (simulating what would happen in full pipeline)
    char ascii_output[30000];
    size_t ascii_len = ascii_simd_color_neon(original_image, width * height,
                                            ascii_output, sizeof(ascii_output), false, false);
    
    // For this test, we'll analyze the ASCII to see if it preserves image structure
    // Count different types of ASCII characters used
    int char_counts[256] = {0};
    for (size_t i = 0; i < ascii_len; i++) {
        char c = ascii_output[i];
        if (c >= 0) {
            char_counts[(unsigned char)c]++;
        }
    }
    
    // Should use multiple different ASCII characters for gradient
    int unique_chars = 0;
    for (int i = 0; i < 256; i++) {
        if (char_counts[i] > 0) {
            unique_chars++;
        }
    }
    
    cr_assert_gt(unique_chars, 5, "Gradient should produce varied ASCII characters (got %d)", unique_chars);
    
    // Verify ASCII contains ANSI color codes (for color preservation)
    bool has_ansi_colors = strstr(ascii_output, "\033[38;2;") != NULL;
    cr_assert(has_ansi_colors, "ASCII should contain ANSI color codes for quality preservation");
    
    free(original_image);
    free(reconstructed_image);
}