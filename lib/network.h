#pragma once

#include "platform/abstraction.h"
#include <stdbool.h>
#include "common.h"
#include "platform/terminal.h"
#include "crc32.h"

// Pack network protocol structures tightly for wire format
#ifdef _WIN32
#pragma pack(push, 1)
#endif

// Timeout constants (in seconds) - tuned for real-time video streaming
#define CONNECT_TIMEOUT 3 // Reduced for faster connection attempts
#define SEND_TIMEOUT 5    // Video frames need timely delivery
#define RECV_TIMEOUT 15   // If no data in 15 sec, connection is likely dead
#define ACCEPT_TIMEOUT 3  // Balance between responsiveness and CPU usage

// Keep-alive settings
#define KEEPALIVE_IDLE 60
#define KEEPALIVE_INTERVAL 10
#define KEEPALIVE_COUNT 8

// Network utility functions
int set_socket_timeout(socket_t sockfd, int timeout_seconds);
int set_socket_nonblocking(socket_t sockfd);
int set_socket_keepalive(socket_t sockfd);
bool connect_with_timeout(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout_seconds);
ssize_t send_with_timeout(socket_t sockfd, const void *buf, size_t len, int timeout_seconds);
ssize_t recv_with_timeout(socket_t sockfd, void *buf, size_t len, int timeout_seconds);
int accept_with_timeout(socket_t listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds);

// Error handling
const char *network_error_string(int error_code);

// Size communication protocol
int send_size_message(socket_t sockfd, unsigned short width, unsigned short height);
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
#define MAX_PACKET_SIZE ((size_t)5 * 1024 * 1024) // 5MB max packet size

typedef enum {
  // Unified frame packets (header + data in single packet)
  PACKET_TYPE_ASCII_FRAME = 1, // Complete ASCII frame with all metadata
  PACKET_TYPE_IMAGE_FRAME = 2, // Complete RGB image with dimensions

  // Audio and control
  PACKET_TYPE_AUDIO = 3,
  PACKET_TYPE_CLIENT_CAPABILITIES = 4, // Client reports terminal capabilities
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

  // Crypto handshake packets (ALWAYS SENT UNENCRYPTED)
  PACKET_TYPE_KEY_EXCHANGE_INIT = 14,      // Server -> Client: {server_pubkey[32]}
  PACKET_TYPE_KEY_EXCHANGE_RESPONSE = 15,  // Client -> Server: {client_pubkey[32]}
  PACKET_TYPE_AUTH_CHALLENGE = 16,         // Server -> Client: {nonce[32]}
  PACKET_TYPE_AUTH_RESPONSE = 17,          // Client -> Server: {HMAC[32]}
  PACKET_TYPE_HANDSHAKE_COMPLETE = 18,     // Server -> Client: "encryption ready"
  PACKET_TYPE_AUTH_FAILED = 19,            // Server -> Client: "authentication failed"
  PACKET_TYPE_ENCRYPTED = 20               // Encrypted packet (after handshake)
} packet_type_t;

typedef struct {
  uint32_t magic;     // PACKET_MAGIC for packet validation
  uint16_t type;      // packet_type_t
  uint32_t length;    // payload length
  uint32_t crc32;     // payload checksum
  uint32_t client_id; // which client this packet is from (0 = server)
} PACKED_ATTR packet_header_t;
// Multi-user protocol structures
#define ASCIICHAT_DEFAULT_DISPLAY_NAME "AsciiChatter"
#define MAX_DISPLAY_NAME_LEN 32
#define MAX_CLIENTS 10

// Size packet for terminal size updates
typedef struct {
  uint32_t width;
  uint32_t height;
} size_packet_t;

typedef struct {
  uint32_t client_id;                      // Unique client identifier
  char display_name[MAX_DISPLAY_NAME_LEN]; // User display name
  uint32_t capabilities;                   // Bitmask: VIDEO_CAPABLE | AUDIO_CAPABLE
} PACKED_ATTR client_info_packet_t;
typedef struct {
  uint32_t client_id;   // Which client this stream is from
  uint32_t stream_type; // VIDEO_STREAM | AUDIO_STREAM
  uint32_t timestamp;   // When frame was captured
} PACKED_ATTR stream_header_t;
typedef struct {
  uint32_t client_count;                     // Number of clients in list
  client_info_packet_t clients[MAX_CLIENTS]; // Client info array
} PACKED_ATTR client_list_packet_t;
// Server state packet - sent to clients when state changes
typedef struct {
  uint32_t connected_client_count; // Number of currently connected clients
  uint32_t active_client_count;    // Number of clients actively sending video
  uint32_t reserved[6];            // Reserved for future use
} PACKED_ATTR server_state_packet_t;
// Terminal capabilities packet - sent by client to inform server of capabilities
typedef struct {
  uint32_t capabilities;      // Bitmask of TERM_CAP_* flags
  uint32_t color_level;       // terminal_color_level_t enum value
  uint32_t color_count;       // Actual color count (16, 256, 16777216)
  uint32_t render_mode;       // render_mode_t enum value (foreground/background/half-block)
  uint16_t width, height;     // Terminal dimensions
  char term_type[32];         // $TERM value for debugging
  char colorterm[32];         // $COLORTERM value for debugging
  uint8_t detection_reliable; // True if detection methods were reliable
  uint32_t utf8_support;      // 0=no UTF-8, 1=UTF-8 supported
  uint32_t palette_type;      // palette_type_t enum value
  char palette_custom[64];    // Custom palette chars (if palette_type == PALETTE_CUSTOM)
  uint8_t desired_fps;        // Client's desired frame rate (1-144 FPS)
  uint8_t reserved[2];        // Padding for alignment
} PACKED_ATTR terminal_capabilities_packet_t;
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
} PACKED_ATTR ascii_frame_packet_t;
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
} PACKED_ATTR image_frame_packet_t;
// Frame flags for ascii_frame_packet_t
#define FRAME_FLAG_HAS_COLOR 0x01      // Frame includes ANSI color codes
#define FRAME_FLAG_IS_COMPRESSED 0x02  // Frame data is zlib compressed
#define FRAME_FLAG_RLE_COMPRESSED 0x04 // Frame data is RLE compressed (new)
#define FRAME_FLAG_IS_STRETCHED 0x04   // Frame was stretched (aspect adjusted)

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
} PACKED_ATTR audio_batch_packet_t;
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
int send_audio_data(socket_t sockfd, const float *samples, int num_samples);
int receive_audio_data(socket_t sockfd, float *samples, int max_samples);

int send_packet(socket_t sockfd, packet_type_t type, const void *data, size_t len);
int receive_packet(socket_t sockfd, packet_type_t *type, void **data, size_t *len);

int send_audio_packet(socket_t sockfd, const float *samples, int num_samples);
int send_audio_batch_packet(socket_t sockfd, const float *samples, int num_samples, int batch_count);

// Multi-user protocol functions
int send_client_join_packet(socket_t sockfd, const char *display_name, uint32_t capabilities);
int send_client_leave_packet(socket_t sockfd, uint32_t client_id);
int send_stream_start_packet(socket_t sockfd, uint32_t stream_type);
int send_stream_stop_packet(socket_t sockfd, uint32_t stream_type);

// Packet sending with client ID
int send_packet_from_client(socket_t sockfd, packet_type_t type, uint32_t client_id, const void *data, size_t len);
int receive_packet_with_client(socket_t sockfd, packet_type_t *type, uint32_t *client_id, void **data, size_t *len);

// Receive encrypted packet from client (after crypto handshake)
int receive_encrypted_packet_with_client(socket_t sockfd, packet_type_t *type, uint32_t *client_id, void **data, size_t *len);

// Heartbeat/ping functions
int send_ping_packet(socket_t sockfd);
int send_pong_packet(socket_t sockfd);

// Console control functions
int send_clear_console_packet(socket_t sockfd);
int send_server_state_packet(socket_t sockfd, const server_state_packet_t *state);
int send_terminal_capabilities_packet(socket_t sockfd, const terminal_capabilities_packet_t *caps);
int send_terminal_size_with_auto_detect(socket_t sockfd, unsigned short width,
                                        unsigned short height); // Convenience function with auto-detection

// Frame sending functions
int send_ascii_frame_packet(socket_t sockfd, const char *frame_data, size_t frame_size, int width, int height);
int send_image_frame_packet(socket_t sockfd, const void *pixel_data, size_t pixel_size, int width, int height,
                            uint32_t pixel_format);
int send_compressed_frame(socket_t sockfd, const char *frame_data, size_t frame_size);

#ifdef _WIN32
#pragma pack(pop)
#endif
