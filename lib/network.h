#pragma once

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

// Timeout constants (in seconds)
#define CONNECT_TIMEOUT 10
#define SEND_TIMEOUT 10
#define RECV_TIMEOUT 30
#define ACCEPT_TIMEOUT 10

// Keep-alive settings
#define KEEPALIVE_IDLE 60
#define KEEPALIVE_INTERVAL 10
#define KEEPALIVE_COUNT 8

// Network utility functions
int set_socket_timeout(int sockfd, int timeout_seconds);
int set_socket_nonblocking(int sockfd);
int set_socket_keepalive(int sockfd);
bool connect_with_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout_seconds);
ssize_t send_with_timeout(int sockfd, const void *buf, size_t len, int timeout_seconds);
ssize_t recv_with_timeout(int sockfd, void *buf, size_t len, int timeout_seconds);
int accept_with_timeout(int listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds);

// Error handling
const char *network_error_string(int error_code);

// Size communication protocol
int send_size_message(int sockfd, unsigned short width, unsigned short height);
int parse_size_message(const char *message, unsigned short *width, unsigned short *height);

/* ============================================================================
 * Protocol Definitions
 * ============================================================================
 */

/* Size communication protocol */
#define SIZE_MESSAGE_PREFIX "SIZE:"
#define SIZE_MESSAGE_FORMAT "SIZE:%u,%u\n"
#define SIZE_MESSAGE_MAX_LEN 32

/* Audio communication protocol */
#define AUDIO_MESSAGE_PREFIX "AUDIO:"
#define AUDIO_MESSAGE_FORMAT "AUDIO:%u\n"
#define AUDIO_MESSAGE_MAX_LEN 32
#define AUDIO_SAMPLES_PER_PACKET 256 // Smaller packets for lower latency

/* Audio batching for efficiency */
#define AUDIO_BATCH_COUNT 4                                                // Number of audio chunks per batch
#define AUDIO_BATCH_SAMPLES (AUDIO_SAMPLES_PER_PACKET * AUDIO_BATCH_COUNT) // 1024 samples = 23.2ms @ 44.1kHz
#define AUDIO_BATCH_MS 23                                                  // Approximate milliseconds per batch

/* Packet-based communication protocol */
#define PACKET_MAGIC 0xDEADBEEF
#define MAX_PACKET_SIZE (5 * 1024 * 1024) // 5MB max packet size

typedef enum {
  // Unified frame packets (header + data in single packet)
  PACKET_TYPE_ASCII_FRAME = 1, // Complete ASCII frame with all metadata
  PACKET_TYPE_IMAGE_FRAME = 2, // Complete RGB image with dimensions

  // Audio and control
  PACKET_TYPE_AUDIO = 3,
  PACKET_TYPE_SIZE = 4,
  PACKET_TYPE_PING = 5,
  PACKET_TYPE_PONG = 6,

  // Multi-user protocol extensions
  PACKET_TYPE_CLIENT_JOIN = 7,    // Client announces capability to send media
  PACKET_TYPE_CLIENT_LEAVE = 8,   // Clean disconnect notification
  PACKET_TYPE_STREAM_START = 9,   // Client requests to start sending video/audio
  PACKET_TYPE_STREAM_STOP = 10,   // Client stops sending media
  PACKET_TYPE_CLEAR_CONSOLE = 11, // Server tells client to clear console
  PACKET_TYPE_SERVER_STATE = 12,  // Server sends current state to clients
  PACKET_TYPE_AUDIO_BATCH = 13,   // Batched audio packets for efficiency
} packet_type_t;

typedef struct {
  uint32_t magic;     // PACKET_MAGIC for packet validation
  uint16_t type;      // packet_type_t
  uint32_t length;    // payload length
  uint32_t crc32;     // payload checksum
  uint32_t client_id; // which client this packet is from (0 = server)
} __attribute__((packed)) packet_header_t;

// Multi-user protocol structures
#define ASCIICHAT_DEFAULT_DISPLAY_NAME "AsciiChatter"
#define MAX_DISPLAY_NAME_LEN 32
#define MAX_CLIENTS 10

typedef struct {
  uint32_t client_id;                      // Unique client identifier
  char display_name[MAX_DISPLAY_NAME_LEN]; // User display name
  uint32_t capabilities;                   // Bitmask: VIDEO_CAPABLE | AUDIO_CAPABLE
} __attribute__((packed)) client_info_packet_t;

typedef struct {
  uint32_t client_id;   // Which client this stream is from
  uint32_t stream_type; // VIDEO_STREAM | AUDIO_STREAM
  uint32_t timestamp;   // When frame was captured
} __attribute__((packed)) stream_header_t;

typedef struct {
  uint32_t client_count;                     // Number of clients in list
  client_info_packet_t clients[MAX_CLIENTS]; // Client info array
} __attribute__((packed)) client_list_packet_t;

// Server state packet - sent to clients when state changes
typedef struct {
  uint32_t connected_client_count; // Number of currently connected clients
  uint32_t active_client_count;    // Number of clients actively sending video
  uint32_t reserved[6];            // Reserved for future use
} __attribute__((packed)) server_state_packet_t;

// ============================================================================
// Unified Frame Packet Structures
// ============================================================================

// ASCII frame packet - contains complete ASCII art frame with metadata
// This replaces the old two-packet system (VIDEO_HEADER + VIDEO)
typedef struct {
  // Frame metadata
  uint32_t width;           // Terminal width in characters
  uint32_t height;          // Terminal height in characters
  uint32_t original_size;   // Size of uncompressed ASCII data
  uint32_t compressed_size; // Size of compressed data (0 = not compressed)
  uint32_t checksum;        // CRC32 of original ASCII data
  uint32_t flags;           // Bit flags: HAS_COLOR, IS_COMPRESSED, etc.

  // The actual ASCII frame data follows this header in the packet payload
  // If compressed_size > 0, data is zlib compressed
  // Format: char data[original_size] or compressed_data[compressed_size]
} __attribute__((packed)) ascii_frame_packet_t;

// Image frame packet - contains raw RGB image with dimensions
// Used when client sends camera frames to server
typedef struct {
  uint32_t width;           // Image width in pixels
  uint32_t height;          // Image height in pixels
  uint32_t pixel_format;    // Format: 0=RGB, 1=RGBA, 2=BGR, etc.
  uint32_t compressed_size; // If >0, image is compressed
  uint32_t checksum;        // CRC32 of pixel data
  uint32_t timestamp;       // When frame was captured

  // The actual pixel data follows this header in the packet payload
  // Format: rgb_t pixels[width * height] or compressed data
} __attribute__((packed)) image_frame_packet_t;

// Frame flags for ascii_frame_packet_t
#define FRAME_FLAG_HAS_COLOR 0x01     // Frame includes ANSI color codes
#define FRAME_FLAG_IS_COMPRESSED 0x02 // Frame data is zlib compressed
#define FRAME_FLAG_IS_STRETCHED 0x04  // Frame was stretched (aspect adjusted)

// Pixel formats for image_frame_packet_t
#define PIXEL_FORMAT_RGB 0
#define PIXEL_FORMAT_RGBA 1
#define PIXEL_FORMAT_BGR 2
#define PIXEL_FORMAT_BGRA 3

// Audio batch packet - contains multiple audio chunks for efficiency
typedef struct {
  uint32_t batch_count;   // Number of audio chunks in this batch (usually AUDIO_BATCH_COUNT)
  uint32_t total_samples; // Total samples across all chunks
  uint32_t sample_rate;   // Sample rate (e.g., 44100)
  uint32_t channels;      // Number of channels (1=mono, 2=stereo)
  // The actual audio data follows: float samples[total_samples]
} __attribute__((packed)) audio_batch_packet_t;

// Capability flags
#define CLIENT_CAP_VIDEO 0x01
#define CLIENT_CAP_AUDIO 0x02
#define CLIENT_CAP_COLOR 0x04
#define CLIENT_CAP_STRETCH 0x08

// Stream type flags
#define STREAM_TYPE_VIDEO 0x01
#define STREAM_TYPE_AUDIO 0x02

/* ============================================================================
 * Protocol Function Declarations
 * ============================================================================
 */

/* Protocol functions */
int send_audio_data(int sockfd, const float *samples, int num_samples);
int receive_audio_data(int sockfd, float *samples, int max_samples);

/* Packet protocol functions */
// CRC32 function moved to crc32_hw.h for hardware acceleration
#include "crc32_hw.h"

int send_packet(int sockfd, packet_type_t type, const void *data, size_t len);
int receive_packet(int sockfd, packet_type_t *type, void **data, size_t *len);

int send_audio_packet(int sockfd, const float *samples, int num_samples);
int send_audio_batch_packet(int sockfd, const float *samples, int num_samples, int batch_count);
int send_size_packet(int sockfd, unsigned short width, unsigned short height);

// Multi-user protocol functions
int send_client_join_packet(int sockfd, const char *display_name, uint32_t capabilities);
int send_client_leave_packet(int sockfd, uint32_t client_id);
int send_client_list_packet(int sockfd, const client_list_packet_t *client_list);
int send_stream_start_packet(int sockfd, uint32_t stream_type);
int send_stream_stop_packet(int sockfd, uint32_t stream_type);

// Packet sending with client ID
int send_packet_from_client(int sockfd, packet_type_t type, uint32_t client_id, const void *data, size_t len);
int receive_packet_with_client(int sockfd, packet_type_t *type, uint32_t *client_id, void **data, size_t *len);

// Heartbeat/ping functions
int send_ping_packet(int sockfd);
int send_pong_packet(int sockfd);

// Console control functions
int send_clear_console_packet(int sockfd);
int send_server_state_packet(int sockfd, const server_state_packet_t *state);
