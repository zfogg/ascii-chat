#ifndef NETWORK_H
#define NETWORK_H

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

// Timeout constants (in seconds)
#define CONNECT_TIMEOUT 10
#define SEND_TIMEOUT 5
#define RECV_TIMEOUT 5
#define ACCEPT_TIMEOUT 30

// Keep-alive settings
#define KEEPALIVE_IDLE 60
#define KEEPALIVE_INTERVAL 10
#define KEEPALIVE_COUNT 3

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

/* Packet-based communication protocol */
#define PACKET_MAGIC 0xDEADBEEF
#define MAX_PACKET_SIZE (1024 * 1024) // 1MB max packet size

typedef enum {
  PACKET_TYPE_VIDEO_HEADER = 1,
  PACKET_TYPE_VIDEO = 2,
  PACKET_TYPE_AUDIO = 3,
  PACKET_TYPE_SIZE = 4,
  PACKET_TYPE_PING = 5,
  PACKET_TYPE_PONG = 6,
  // NEW MULTI-USER TYPES:
  PACKET_TYPE_CLIENT_JOIN = 7,     // Client announces capability to send media
  PACKET_TYPE_CLIENT_LEAVE = 8,    // Clean disconnect notification  
  PACKET_TYPE_CLIENT_LIST = 9,     // Server sends list of active clients
  PACKET_TYPE_STREAM_START = 10,   // Client requests to start sending video/audio
  PACKET_TYPE_STREAM_STOP = 11,    // Client stops sending media
  PACKET_TYPE_MIXED_AUDIO = 12,    // Server sends mixed audio from all clients
} packet_type_t;

typedef struct {
  uint32_t magic;    // PACKET_MAGIC for packet validation
  uint16_t type;     // packet_type_t
  uint32_t length;   // payload length
  uint32_t sequence; // for ordering/duplicate detection
  uint32_t crc32;    // payload checksum
} __attribute__((packed)) packet_header_t;

/* Multi-user packet structures */
#define CLIENT_NAME_MAX 32

typedef struct {
  uint32_t client_id;           // Unique client identifier
  char display_name[CLIENT_NAME_MAX]; // User display name
  uint32_t capabilities;        // Bitmask: VIDEO_CAPABLE | AUDIO_CAPABLE
  uint16_t video_width;         // Client's video dimensions
  uint16_t video_height;
} __attribute__((packed)) client_info_packet_t;

typedef struct {
  uint32_t client_id;           // Which client this stream is from  
  uint32_t stream_type;         // VIDEO_STREAM | AUDIO_STREAM
  uint32_t sequence;            // For frame ordering
  uint32_t timestamp;           // When frame was captured
} __attribute__((packed)) stream_header_t;

typedef struct {
  uint32_t num_clients;
  // Followed by num_clients * client_info_packet_t entries
} __attribute__((packed)) client_list_packet_t;

// Capability flags
#define CLIENT_CAP_VIDEO (1 << 0)
#define CLIENT_CAP_AUDIO (1 << 1)

// Stream type flags  
#define STREAM_TYPE_VIDEO 1
#define STREAM_TYPE_AUDIO 2

/* ============================================================================
 * Protocol Function Declarations
 * ============================================================================
 */

/* Protocol functions */
int send_size_message(int sockfd, unsigned short width, unsigned short height);
int parse_size_message(const char *message, unsigned short *width, unsigned short *height);
int send_audio_data(int sockfd, const float *samples, int num_samples);
int receive_audio_data(int sockfd, float *samples, int max_samples);

/* Packet protocol functions */
uint32_t asciichat_crc32(const void *data, size_t len);

uint32_t get_next_sequence(void);

int send_packet(int sockfd, packet_type_t type, const void *data, size_t len);
int receive_packet(int sockfd, packet_type_t *type, void **data, size_t *len);

int send_video_header_packet(int sockfd, const void *header_data, size_t header_len);
int send_video_packet(int sockfd, const void *frame_data, size_t frame_len);
int send_audio_packet(int sockfd, const float *samples, int num_samples);
int send_size_packet(int sockfd, unsigned short width, unsigned short height);

/* Multi-user protocol functions */
int send_client_join_packet(int sockfd, uint32_t client_id, const char *display_name, 
                           uint32_t capabilities, uint16_t width, uint16_t height);
int send_client_leave_packet(int sockfd, uint32_t client_id);
int send_client_list_packet(int sockfd, const client_info_packet_t *clients, uint32_t num_clients);
int send_stream_start_packet(int sockfd, uint32_t client_id, uint32_t stream_type);
int send_stream_stop_packet(int sockfd, uint32_t client_id, uint32_t stream_type);
int send_mixed_audio_packet(int sockfd, const float *samples, int num_samples);
int send_stream_packet(int sockfd, uint32_t client_id, uint32_t stream_type, 
                      const void *data, size_t len);

#endif // NETWORK_H
