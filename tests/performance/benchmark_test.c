#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdlib.h>

#include "common.h"
#include "ascii_simd.h"
#include "ascii_simd_color.h"
#include "network.h"
#include "audio.h"
#include "mixer.h"
#include "compression.h"

void setup_benchmark_quiet_logging(void);
void restore_benchmark_logging(void);

TestSuite(benchmark, .init = setup_benchmark_quiet_logging, .fini = restore_benchmark_logging);

void setup_benchmark_quiet_logging(void) {
    log_set_level(LOG_ERROR); // Show errors but suppress info/debug
}

void restore_benchmark_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// Timing Utilities
// =============================================================================

static double get_time_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static size_t get_memory_usage(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss; // Peak memory usage in KB (Linux) or bytes (macOS)
}

static void create_benchmark_image(rgb_pixel_t *pixels, int width, int height, int pattern) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            switch (pattern) {
                case 0: // Random noise
                    pixels[idx] = (rgb_pixel_t){
                        .r = rand() % 256,
                        .g = rand() % 256,
                        .b = rand() % 256
                    };
                    break;
                case 1: // Gradient
                    pixels[idx] = (rgb_pixel_t){
                        .r = (x * 255) / width,
                        .g = (y * 255) / height,
                        .b = ((x + y) * 255) / (width + height)
                    };
                    break;
                case 2: // High contrast
                    pixels[idx] = ((x + y) % 2) ? 
                        (rgb_pixel_t){255, 255, 255} : 
                        (rgb_pixel_t){0, 0, 0};
                    break;
                default:
                    pixels[idx] = (rgb_pixel_t){128, 128, 128};
                    break;
            }
        }
    }
}

// =============================================================================
// SIMD Performance Benchmarks
// =============================================================================

Test(benchmark, simd_scalar_vs_neon_performance) {
    const int width = 320, height = 240;
    const int iterations = 20;
    rgb_pixel_t *pixels;
    
    SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    create_benchmark_image(pixels, width, height, 1); // Gradient pattern
    
    char output[width * height * 25]; // Large buffer for ANSI codes
    
    // Benchmark scalar implementation
    double start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        ascii_simd_color_scalar(pixels, width * height, output, sizeof(output), false, false);
    }
    double scalar_time = get_time_seconds() - start_time;
    
    // Benchmark NEON implementation
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        ascii_simd_color_neon(pixels, width * height, output, sizeof(output), false, false);
    }
    double neon_time = get_time_seconds() - start_time;
    
    // Calculate performance metrics
    double scalar_fps = iterations / scalar_time;
    double neon_fps = iterations / neon_time;
    double speedup = scalar_time / neon_time;
    
    log_info("SIMD Performance (%dx%d, %d iterations):", width, height, iterations);
    log_info("  Scalar: %.3fs (%.1f FPS)", scalar_time, scalar_fps);
    log_info("  NEON:   %.3fs (%.1f FPS)", neon_time, neon_fps);
    log_info("  Speedup: %.2fx", speedup);
    
    // Performance expectations
    cr_assert_gt(scalar_fps, 1.0, "Scalar should achieve at least 1 FPS");
    cr_assert_gt(neon_fps, 1.0, "NEON should achieve at least 1 FPS");
    cr_assert_gt(speedup, 0.5, "NEON should not be significantly slower than scalar");
    
    free(pixels);
}

Test(benchmark, simd_different_image_sizes) {
    typedef struct {
        int width, height;
        const char *name;
    } test_size_t;
    
    test_size_t sizes[] = {
        {80, 60, "80x60"},
        {160, 120, "160x120"},
        {320, 240, "320x240"},
        {640, 480, "640x480"},
        {80, 24, "80x24 (terminal)"},
        {132, 43, "132x43 (wide terminal)"}
    };
    
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);
    const int iterations = 10;
    
    log_info("SIMD performance across different image sizes:");
    
    for (int s = 0; s < num_sizes; s++) {
        int width = sizes[s].width;
        int height = sizes[s].height;
        rgb_pixel_t *pixels;
        
        SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
        create_benchmark_image(pixels, width, height, 0); // Random pattern
        
        char output[width * height * 25];
        
        // Benchmark NEON
        double start_time = get_time_seconds();
        for (int i = 0; i < iterations; i++) {
            ascii_simd_color_neon(pixels, width * height, output, sizeof(output), false, false);
        }
        double neon_time = get_time_seconds() - start_time;
        
        double fps = iterations / neon_time;
        double pixels_per_sec = (width * height * iterations) / neon_time;
        
        log_info("  %s: %.3fs (%.1f FPS, %.0f pixels/sec)", 
                sizes[s].name, neon_time, fps, pixels_per_sec);
        
        // Should handle at least small images efficiently
        if (width * height <= 80 * 60) {
            cr_assert_gt(fps, 10.0, "%s should achieve at least 10 FPS", sizes[s].name);
        }
        
        free(pixels);
    }
}

Test(benchmark, background_vs_foreground_performance) {
    const int width = 160, height = 120;
    const int iterations = 15;
    rgb_pixel_t *pixels;
    
    SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
    create_benchmark_image(pixels, width, height, 2); // High contrast pattern
    
    char fg_output[width * height * 15];
    char bg_output[width * height * 30]; // Background mode needs more space
    
    // Benchmark foreground mode
    double start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        ascii_simd_color_neon(pixels, width * height, fg_output, sizeof(fg_output), false, false);
    }
    double fg_time = get_time_seconds() - start_time;
    
    // Benchmark background mode
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        ascii_simd_color_neon(pixels, width * height, bg_output, sizeof(bg_output), true, false);
    }
    double bg_time = get_time_seconds() - start_time;
    
    double fg_fps = iterations / fg_time;
    double bg_fps = iterations / bg_time;
    double bg_overhead = bg_time / fg_time;
    
    log_info("Color Mode Performance (%dx%d, %d iterations):", width, height, iterations);
    log_info("  Foreground: %.3fs (%.1f FPS)", fg_time, fg_fps);
    log_info("  Background: %.3fs (%.1f FPS)", bg_time, bg_fps);
    log_info("  BG Overhead: %.2fx slower", bg_overhead);
    
    // Background mode should be slower but not excessively so
    cr_assert_gt(bg_fps, 1.0, "Background mode should achieve at least 1 FPS");
    cr_assert_lt(bg_overhead, 5.0, "Background mode should not be more than 5x slower");
    
    free(pixels);
}

// =============================================================================
// Network Performance Benchmarks
// =============================================================================

Test(benchmark, packet_serialization_performance) {
    const int packet_count = 10000;
    const char *test_data = "Test ASCII frame data with reasonable length for network transmission benchmarking";
    
    packet_t *packets;
    SAFE_MALLOC(packets, packet_count * sizeof(packet_t), packet_t*);
    
    uint8_t *buffers;
    SAFE_MALLOC(buffers, packet_count * MAX_PACKET_SIZE, uint8_t*);
    
    // Create test packets
    double start_time = get_time_seconds();
    for (int i = 0; i < packet_count; i++) {
        create_ascii_frame_packet(&packets[i], test_data, strlen(test_data), i + 1000);
    }
    double creation_time = get_time_seconds() - start_time;
    
    // Benchmark serialization
    start_time = get_time_seconds();
    size_t total_serialized = 0;
    for (int i = 0; i < packet_count; i++) {
        size_t packet_size = serialize_packet(&packets[i], 
                                            buffers + i * MAX_PACKET_SIZE, 
                                            MAX_PACKET_SIZE);
        total_serialized += packet_size;
    }
    double serialization_time = get_time_seconds() - start_time;
    
    // Benchmark deserialization
    packet_t *deserialized_packets;
    SAFE_MALLOC(deserialized_packets, packet_count * sizeof(packet_t), packet_t*);
    
    start_time = get_time_seconds();
    for (int i = 0; i < packet_count; i++) {
        deserialize_packet(buffers + i * MAX_PACKET_SIZE, 
                          sizeof(packet_header_t) + strlen(test_data),
                          &deserialized_packets[i]);
    }
    double deserialization_time = get_time_seconds() - start_time;
    
    // Calculate performance metrics
    double creation_rate = packet_count / creation_time;
    double serialization_rate = packet_count / serialization_time;
    double deserialization_rate = packet_count / deserialization_time;
    double throughput_mbps = (total_serialized * 8.0) / (serialization_time * 1000000.0);
    
    log_info("Packet Performance (%d packets):", packet_count);
    log_info("  Creation: %.3fs (%.0f packets/sec)", creation_time, creation_rate);
    log_info("  Serialization: %.3fs (%.0f packets/sec, %.1f Mbps)", 
             serialization_time, serialization_rate, throughput_mbps);
    log_info("  Deserialization: %.3fs (%.0f packets/sec)", 
             deserialization_time, deserialization_rate);
    
    // Performance expectations for packet processing
    cr_assert_gt(creation_rate, 1000.0, "Should create at least 1000 packets/sec");
    cr_assert_gt(serialization_rate, 1000.0, "Should serialize at least 1000 packets/sec");
    cr_assert_gt(deserialization_rate, 1000.0, "Should deserialize at least 1000 packets/sec");
    
    // Cleanup
    for (int i = 0; i < packet_count; i++) {
        free(packets[i].data);
        if (deserialized_packets[i].data) {
            free(deserialized_packets[i].data);
        }
    }
    free(packets);
    free(buffers);
    free(deserialized_packets);
}

Test(benchmark, crc32_performance) {
    const int data_sizes[] = {64, 256, 1024, 4096, 16384, 65536};
    const int num_sizes = sizeof(data_sizes) / sizeof(data_sizes[0]);
    const int iterations = 1000;
    
    log_info("CRC32 Performance Benchmark:");
    
    for (int s = 0; s < num_sizes; s++) {
        int size = data_sizes[s];
        uint8_t *test_data;
        SAFE_MALLOC(test_data, size, uint8_t*);
        
        // Fill with random data
        for (int i = 0; i < size; i++) {
            test_data[i] = rand() % 256;
        }
        
        // Benchmark CRC32 calculation
        double start_time = get_time_seconds();
        for (int i = 0; i < iterations; i++) {
            uint32_t crc = crc32_calculate(0, test_data, size);
            (void)crc; // Prevent compiler optimization
        }
        double crc_time = get_time_seconds() - start_time;
        
        double throughput_mbps = (size * iterations * 8.0) / (crc_time * 1000000.0);
        double rate = iterations / crc_time;
        
        log_info("  %d bytes: %.3fs (%.0f calcs/sec, %.1f MB/s)", 
                size, crc_time, rate, throughput_mbps / 8.0);
        
        // Should achieve reasonable throughput
        cr_assert_gt(rate, 100.0, "CRC32 should compute at least 100 checksums/sec for %d bytes", size);
        
        free(test_data);
    }
}

// =============================================================================
// Audio Performance Benchmarks
// =============================================================================

Test(benchmark, audio_mixing_performance) {
    const int sample_rates[] = {22050, 44100, 48000, 96000};
    const int num_rates = sizeof(sample_rates) / sizeof(sample_rates[0]);
    const int duration_ms = 100; // Process 100ms of audio
    const int iterations = 50;
    
    log_info("Audio Mixing Performance:");
    
    for (int r = 0; r < num_rates; r++) {
        int sample_rate = sample_rates[r];
        int sample_count = (sample_rate * duration_ms) / 1000;
        
        float *input1, *input2, *output;
        SAFE_MALLOC(input1, sample_count * sizeof(float), float*);
        SAFE_MALLOC(input2, sample_count * sizeof(float), float*);
        SAFE_MALLOC(output, sample_count * sizeof(float), float*);
        
        // Generate test audio
        for (int i = 0; i < sample_count; i++) {
            input1[i] = sinf(2.0f * M_PI * 440.0f * i / sample_rate);
            input2[i] = sinf(2.0f * M_PI * 880.0f * i / sample_rate);
        }
        
        // Benchmark audio mixing
        double start_time = get_time_seconds();
        for (int i = 0; i < iterations; i++) {
            mix_audio_samples(input1, input2, output, sample_count);
        }
        double mix_time = get_time_seconds() - start_time;
        
        double samples_per_sec = (sample_count * iterations) / mix_time;
        double realtime_factor = samples_per_sec / sample_rate;
        
        log_info("  %d Hz: %.3fs (%.0f samples/sec, %.1fx realtime)", 
                sample_rate, mix_time, samples_per_sec, realtime_factor);
        
        // Should process audio faster than realtime
        cr_assert_gt(realtime_factor, 2.0, 
                    "Audio mixing should be at least 2x faster than realtime for %d Hz", sample_rate);
        
        free(input1);
        free(input2);
        free(output);
    }
}

Test(benchmark, audio_effects_performance) {
    const int sample_count = 44100; // 1 second at 44.1kHz
    const int iterations = 100;
    
    float *input, *output;
    SAFE_MALLOC(input, sample_count * sizeof(float), float*);
    SAFE_MALLOC(output, sample_count * sizeof(float), float*);
    
    // Create test signal
    for (int i = 0; i < sample_count; i++) {
        input[i] = sinf(2.0f * M_PI * 1000.0f * i / 44100.0f);
    }
    
    // Benchmark gain application
    double start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        apply_audio_gain(input, output, sample_count, 0.8f);
    }
    double gain_time = get_time_seconds() - start_time;
    
    // Benchmark limiting
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        apply_audio_limiter(input, output, sample_count, 1.0f);
    }
    double limiter_time = get_time_seconds() - start_time;
    
    double gain_rate = (sample_count * iterations) / gain_time;
    double limiter_rate = (sample_count * iterations) / limiter_time;
    
    log_info("Audio Effects Performance (%d samples, %d iterations):", sample_count, iterations);
    log_info("  Gain: %.3fs (%.0f samples/sec, %.1fx realtime)", 
            gain_time, gain_rate, gain_rate / 44100.0);
    log_info("  Limiter: %.3fs (%.0f samples/sec, %.1fx realtime)", 
            limiter_time, limiter_rate, limiter_rate / 44100.0);
    
    // Audio effects should be very fast
    cr_assert_gt(gain_rate / 44100.0, 10.0, "Gain should process at least 10x realtime");
    cr_assert_gt(limiter_rate / 44100.0, 5.0, "Limiter should process at least 5x realtime");
    
    free(input);
    free(output);
}

// =============================================================================
// Compression Performance Benchmarks
// =============================================================================

Test(benchmark, compression_performance) {
    typedef struct {
        const char *name;
        int pattern_type;
    } test_pattern_t;
    
    test_pattern_t patterns[] = {
        {"Random", 0},
        {"Gradient", 1}, 
        {"High Contrast", 2}
    };
    
    const int width = 160, height = 120;
    const int iterations = 20;
    int num_patterns = sizeof(patterns) / sizeof(patterns[0]);
    
    log_info("Compression Performance (%dx%d ASCII):", width, height);
    
    for (int p = 0; p < num_patterns; p++) {
        rgb_pixel_t *pixels;
        SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
        create_benchmark_image(pixels, width, height, patterns[p].pattern_type);
        
        // Convert to ASCII
        char ascii_output[width * height * 20];
        size_t ascii_len = ascii_simd_color_scalar(pixels, width * height, 
                                                  ascii_output, sizeof(ascii_output), false, false);
        
        uint8_t compressed_buffer[ascii_len * 2];
        uint8_t decompressed_buffer[ascii_len * 2];
        
        // Benchmark compression
        double start_time = get_time_seconds();
        size_t total_compressed = 0;
        for (int i = 0; i < iterations; i++) {
            size_t compressed_size = compress_data((uint8_t*)ascii_output, ascii_len,
                                                  compressed_buffer, sizeof(compressed_buffer));
            total_compressed += compressed_size;
        }
        double compress_time = get_time_seconds() - start_time;
        
        // Benchmark decompression (if compression succeeded)
        double decompress_time = 0.0;
        if (total_compressed > 0) {
            size_t avg_compressed = total_compressed / iterations;
            
            start_time = get_time_seconds();
            for (int i = 0; i < iterations; i++) {
                decompress_data(compressed_buffer, avg_compressed,
                               decompressed_buffer, sizeof(decompressed_buffer));
            }
            decompress_time = get_time_seconds() - start_time;
        }
        
        if (total_compressed > 0) {
            double compression_ratio = (double)ascii_len / (total_compressed / iterations);
            double compress_mbps = (ascii_len * iterations * 8.0) / (compress_time * 1000000.0);
            double decompress_mbps = (ascii_len * iterations * 8.0) / (decompress_time * 1000000.0);
            
            log_info("  %s: %.1f:1 ratio, %.1f MB/s compress, %.1f MB/s decompress", 
                    patterns[p].name, compression_ratio, compress_mbps / 8.0, decompress_mbps / 8.0);
            
            // Compression should be reasonably fast
            cr_assert_gt(compress_mbps / 8.0, 1.0, "%s compression should achieve at least 1 MB/s", patterns[p].name);
        } else {
            log_info("  %s: compression failed or not beneficial", patterns[p].name);
        }
        
        free(pixels);
    }
}

// =============================================================================
// Memory Usage and Leak Tests
// =============================================================================

Test(benchmark, memory_usage_patterns) {
    const int iterations = 100;
    const int width = 200, height = 150;
    
    size_t initial_memory = get_memory_usage();
    
    log_info("Memory Usage Test (baseline: %zu KB):", initial_memory);
    
    // Test repeated ASCII conversion
    for (int iter = 0; iter < iterations; iter++) {
        rgb_pixel_t *pixels;
        SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
        create_benchmark_image(pixels, width, height, iter % 3);
        
        char ascii_output[width * height * 20];
        ascii_simd_color_neon(pixels, width * height, ascii_output, sizeof(ascii_output), false, false);
        
        // Create and process packet
        packet_t packet;
        create_ascii_frame_packet(&packet, ascii_output, strlen(ascii_output), iter);
        
        uint8_t buffer[MAX_PACKET_SIZE];
        serialize_packet(&packet, buffer, sizeof(buffer));
        
        packet_t received_packet;
        deserialize_packet(buffer, sizeof(packet_header_t) + packet.header.length, &received_packet);
        
        // Cleanup
        free(pixels);
        free(packet.data);
        free(received_packet.data);
        
        // Check memory every 20 iterations
        if ((iter + 1) % 20 == 0) {
            size_t current_memory = get_memory_usage();
            log_info("  After %d iterations: %zu KB (+%zd KB)", 
                    iter + 1, current_memory, current_memory - initial_memory);
        }
    }
    
    size_t final_memory = get_memory_usage();
    size_t leaked_memory = final_memory - initial_memory;
    
    log_info("Final memory usage: %zu KB (+%zd KB)", final_memory, leaked_memory);
    
    // Should not leak significant memory (allow some growth for OS overhead)
    cr_assert_lt(leaked_memory, 10240, "Should not leak more than 10MB of memory"); // 10MB threshold
}

// =============================================================================
// End-to-End Performance Tests
// =============================================================================

Test(benchmark, full_pipeline_performance) {
    const int frame_count = 30;
    const int width = 160, height = 120;
    
    log_info("Full Pipeline Performance Test (%d frames, %dx%d):", frame_count, width, height);
    
    double start_time = get_time_seconds();
    
    for (int frame = 0; frame < frame_count; frame++) {
        // Step 1: Generate image
        rgb_pixel_t *pixels;
        SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);
        create_benchmark_image(pixels, width, height, frame % 3);
        
        // Step 2: ASCII conversion
        char ascii_output[width * height * 20];
        ascii_simd_color_neon(pixels, width * height, ascii_output, sizeof(ascii_output), false, false);
        
        // Step 3: Packet creation
        packet_t packet;
        create_ascii_frame_packet(&packet, ascii_output, strlen(ascii_output), frame + 7000);
        
        // Step 4: Serialization
        uint8_t buffer[MAX_PACKET_SIZE];
        serialize_packet(&packet, buffer, sizeof(buffer));
        
        // Step 5: Deserialization (simulating network receive)
        packet_t received_packet;
        deserialize_packet(buffer, sizeof(packet_header_t) + packet.header.length, &received_packet);
        
        // Cleanup
        free(pixels);
        free(packet.data);
        free(received_packet.data);
    }
    
    double total_time = get_time_seconds() - start_time;
    double fps = frame_count / total_time;
    
    log_info("Full pipeline: %.3fs total (%.1f FPS)", total_time, fps);
    
    // Should achieve reasonable end-to-end performance
    cr_assert_gt(fps, 3.0, "Full pipeline should achieve at least 3 FPS for %dx%d", width, height);
    
    // Performance should be consistent with component benchmarks
    double expected_min_fps = 2.0; // Conservative estimate
    cr_assert_gt(fps, expected_min_fps, "Pipeline performance should meet minimum expectations");
}