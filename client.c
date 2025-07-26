#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "ascii.h"
#include "client.h"
#include "options.h"
#include "network.h"
#include "common.h"
#include "ringbuffer.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

static int sockfd = 0;
static volatile bool g_should_exit = false;
static volatile bool g_connected = false;
static framebuffer_t* g_frame_buffer = NULL;
static pthread_t g_display_thread;
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Client statistics */
typedef struct {
    uint64_t frames_received;
    uint64_t frames_displayed;
    uint64_t frames_dropped;
    uint64_t bytes_received;
    double avg_receive_fps;
    double avg_display_fps;
    uint64_t network_errors;
} client_stats_t;

static client_stats_t g_stats = {0};

/* ============================================================================
 * Signal Handlers
 * ============================================================================ */

void sigint_handler(int sigint) {
    (void) (sigint);
    g_should_exit = true;
    log_info("Client shutdown requested");
}

void sigwinch_handler(int sigwinch) {
    (void) (sigwinch);
    // Terminal was resized, update dimensions and recalculate aspect ratio
    recalculate_aspect_ratio_on_resize();
    log_debug("Terminal resized, recalculated aspect ratio");
}

/* ============================================================================
 * Display Thread
 * ============================================================================ */

void* display_thread_func(void* arg) {
    (void)arg;
    
    char frame_buffer[FRAME_BUFFER_SIZE];
    struct timespec last_display_time = {0, 0};
    uint64_t frames_displayed = 0;
    
    log_info("Display thread started");
    
    // Initialize ASCII display
    if (ascii_write_init() != ASCIICHAT_OK) {
        log_error("Failed to initialize ASCII display");
        return NULL;
    }
    
    while (!g_should_exit) {
        // Frame rate limiting for display
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        long elapsed_ms = (current_time.tv_sec - last_display_time.tv_sec) * 1000 +
                         (current_time.tv_nsec - last_display_time.tv_nsec) / 1000000;
        
        if (elapsed_ms < FRAME_INTERVAL_MS) {
            usleep((FRAME_INTERVAL_MS - elapsed_ms) * 1000);
            continue;
        }
        
        // Try to get a frame from buffer
        if (!framebuffer_read_frame(g_frame_buffer, frame_buffer)) {
            // No frames available
            if (g_connected) {
                // Connected but no frames - just wait
                usleep(1000); // 1ms
            } else {
                // Not connected - wait longer
                usleep(10000); // 10ms
            }
            continue;
        }
        
        // Display the frame
        if (ascii_write(frame_buffer) != ASCIICHAT_OK) {
            log_error("Failed to display ASCII frame");
        }
        
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.frames_displayed++;
        pthread_mutex_unlock(&g_stats_mutex);
        
        frames_displayed++;
        last_display_time = current_time;
        
        // Update FPS statistics every 30 frames
        if (frames_displayed % 30 == 0 && elapsed_ms > 0) {
            double fps = 30000.0 / elapsed_ms;
            pthread_mutex_lock(&g_stats_mutex);
            g_stats.avg_display_fps = (g_stats.avg_display_fps * 0.9) + (fps * 0.1);
            pthread_mutex_unlock(&g_stats_mutex);
        }
    }
    
    ascii_write_destroy();
    log_info("Display thread stopped");
    return NULL;
}

/* ============================================================================
 * Connection Management
 * ============================================================================ */

/* Exponential backoff for reconnection */
static int calculate_backoff(int attempt) {
    int base_delay = 1;  // 1 second
    int max_delay = 30;  // 30 seconds
    int delay = base_delay << (attempt - 1);  // 2^(attempt-1) seconds
    return (delay > max_delay) ? max_delay : delay;
}

static bool connect_to_server(const char* address, int port) {
    struct sockaddr_in serv_addr;
    
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_error("Socket creation failed: %s", strerror(errno));
        return false;
    }
    
    // Setup server address
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, address, &serv_addr.sin_addr) <= 0) {
        log_error("Invalid address: %s", address);
        close(sockfd);
        sockfd = 0;
        return false;
    }
    
    log_info("Attempting connection to %s:%d", address, port);
    printf("Attempting to connect to %s:%d...\n", address, port);
    
    // Connect with timeout
    if (!connect_with_timeout(sockfd, (struct sockaddr *)&serv_addr, 
                             sizeof(serv_addr), CONNECT_TIMEOUT)) {
        log_error("Connection failed: %s", network_error_string(errno));
        fprintf(stderr, "Error: %s\n", network_error_string(errno));
        close(sockfd);
        sockfd = 0;
        return false;
    }
    
    // Set keep-alive
    if (set_socket_keepalive(sockfd) < 0) {
        log_warn("Failed to set keep-alive: %s", strerror(errno));
    }
    
    log_info("Connected successfully to %s:%d", address, port);
    printf("Connected successfully!\n");
    return true;
}

/* ============================================================================
 * Main Client Logic
 * ============================================================================ */

int main(int argc, char *argv[]) {
    // Initialize logging
    log_init("client.log", LOG_DEBUG);
    log_info("ASCII Chat Client starting...");
    
    options_init(argc, argv);
    char *address = opt_address;
    int port = strtoint(opt_port);

    char *recvBuff = NULL;
    
    // Allocate receive buffer
    recvBuff = (char *)malloc(RECV_BUFFER_SIZE);
    if (!recvBuff) {
        log_fatal("Failed to allocate receive buffer");
        exit(1);
    }

    // Create frame buffer (buffer 5 frames for smooth playback)
    g_frame_buffer = framebuffer_create(5, FRAME_BUFFER_SIZE);
    if (!g_frame_buffer) {
        log_fatal("Failed to create frame buffer");
        free(recvBuff);
        exit(1);
    }

    // Setup signal handlers
    signal(SIGINT, sigint_handler);
    signal(SIGWINCH, sigwinch_handler);

    // Start display thread
    if (pthread_create(&g_display_thread, NULL, display_thread_func, NULL) != 0) {
        log_fatal("Failed to create display thread");
        framebuffer_destroy(g_frame_buffer);
        free(recvBuff);
        exit(1);
    }

    log_info("Connecting to %s:%d", address, port);
    
    int reconnect_attempt = 0;
    struct timespec last_stats_time = {0, 0};
    
    /* Main connection loop with automatic reconnection */
    while (!g_should_exit) {
        // Calculate reconnection delay
        if (reconnect_attempt > 0) {
            int delay = calculate_backoff(reconnect_attempt);
            log_info("Reconnection attempt %d in %d seconds...", reconnect_attempt, delay);
            printf("Reconnection attempt %d in %d seconds...\n", reconnect_attempt, delay);
            sleep(delay);
            
            if (g_should_exit) break;
        }
        
        // Clear receive buffer
        memset(recvBuff, 0, RECV_BUFFER_SIZE);
        
        // Attempt connection
        if (!connect_to_server(address, port)) {
            reconnect_attempt++;
            pthread_mutex_lock(&g_stats_mutex);
            g_stats.network_errors++;
            pthread_mutex_unlock(&g_stats_mutex);
            continue;
        }
        
        // Connection successful
        g_connected = true;
        reconnect_attempt = 0;
        
        // Reset stats for this connection
        pthread_mutex_lock(&g_stats_mutex);
        g_stats.frames_received = 0;
        g_stats.bytes_received = 0;
        pthread_mutex_unlock(&g_stats_mutex);

        // Main receive loop
        while (!g_should_exit && g_connected) {
            ssize_t read_result = recv_with_timeout(sockfd, recvBuff, RECV_BUFFER_SIZE - 1, RECV_TIMEOUT);
            
            if (read_result < 0) {
                if (errno == ETIMEDOUT) {
                    log_debug("Receive timeout, checking connection...");
                    // Send a small probe to check if connection is alive
                    if (send(sockfd, "", 0, MSG_NOSIGNAL) < 0) {
                        log_error("Connection lost: %s", network_error_string(errno));
                        g_connected = false;
                        break;
                    }
                    continue;
                }
                log_error("Receive error: %s", network_error_string(errno));
                pthread_mutex_lock(&g_stats_mutex);
                g_stats.network_errors++;
                pthread_mutex_unlock(&g_stats_mutex);
                g_connected = false;
                break;
            }
            
            if (read_result == 0) {
                log_info("Server closed connection");
                g_connected = false;
                break;
            }
            
            recvBuff[read_result] = '\0';
            
            // Check for error messages from server
            if (strcmp(recvBuff, "Webcam capture failed\n") == 0) {
                log_error("Server webcam error: %s", recvBuff);
                continue;
            }
            
            // Try to buffer the frame
            bool buffered = framebuffer_write_frame(g_frame_buffer, recvBuff);
            
            pthread_mutex_lock(&g_stats_mutex);
            g_stats.frames_received++;
            g_stats.bytes_received += read_result;
            if (!buffered) {
                g_stats.frames_dropped++;
                log_debug("Frame buffer full, dropped frame");
            }
            pthread_mutex_unlock(&g_stats_mutex);
            
            // Print periodic statistics
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            long stats_elapsed = (current_time.tv_sec - last_stats_time.tv_sec) * 1000 +
                               (current_time.tv_nsec - last_stats_time.tv_nsec) / 1000000;
            
            if (stats_elapsed >= 10000) { // Every 10 seconds
                pthread_mutex_lock(&g_stats_mutex);
                log_info("Stats: received=%lu, displayed=%lu, dropped=%lu, buffer_size=%zu", 
                        g_stats.frames_received, g_stats.frames_displayed, 
                        g_stats.frames_dropped, ringbuffer_size(g_frame_buffer->rb));
                pthread_mutex_unlock(&g_stats_mutex);
                last_stats_time = current_time;
            }
        }

        // Connection lost
        if (sockfd > 0) {
            close(sockfd);
            sockfd = 0;
        }
        g_connected = false;
        
        if (!g_should_exit) {
            pthread_mutex_lock(&g_stats_mutex);
            log_info("Connection lost after receiving %lu frames, will reconnect...", 
                    g_stats.frames_received);
            pthread_mutex_unlock(&g_stats_mutex);
            reconnect_attempt++;
        }
    }
    
    // Cleanup
    log_info("Client shutting down...");
    g_should_exit = true;
    g_connected = false;
    
    // Wait for display thread to finish
    pthread_join(g_display_thread, NULL);
    
    // Cleanup resources
    if (sockfd > 0) {
        close(sockfd);
    }
    framebuffer_destroy(g_frame_buffer);
    free(recvBuff);
    
    // Final statistics
    pthread_mutex_lock(&g_stats_mutex);
    log_info("Final stats: received=%lu, displayed=%lu, dropped=%lu, errors=%lu", 
             g_stats.frames_received, g_stats.frames_displayed, 
             g_stats.frames_dropped, g_stats.network_errors);
    pthread_mutex_unlock(&g_stats_mutex);
    
    printf("Client shutdown complete.\n");
    log_destroy();
    return 0;
}
