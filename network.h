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

#endif // NETWORK_H

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
#define AUDIO_SAMPLES_PER_PACKET 512

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
} packet_type_t;

typedef struct {
  uint32_t magic;    // PACKET_MAGIC for packet validation
  uint16_t type;     // packet_type_t
  uint32_t length;   // payload length
  uint32_t sequence; // for ordering/duplicate detection
  uint32_t crc32;    // payload checksum
} __attribute__((packed)) packet_header_t;

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
