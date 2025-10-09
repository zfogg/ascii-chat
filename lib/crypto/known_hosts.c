#include "known_hosts.h"
#include "common.h"
#include "keys.h"
#include "ip.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sodium.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define strncasecmp _strnicmp
#else
#include <sys/stat.h>
#include <strings.h>
#endif

#define KNOWN_HOSTS_PATH "~/.ascii-chat/known_hosts"

static char *expand_path(const char *path) {
  if (path[0] == '~') {
    const char *home = platform_getenv("HOME");
    if (!home) {
      // On Windows, try USERPROFILE
      home = platform_getenv("USERPROFILE");
      if (!home)
        return NULL;
    }

    char *expanded;
    SAFE_MALLOC(expanded, strlen(home) + strlen(path), char *);
    sprintf(expanded, "%s%s", home, path + 1);
    return expanded;
  }
  return strdup(path);
}

const char *get_known_hosts_path(void) {
  static char *path = NULL;
  if (!path) {
    path = expand_path(KNOWN_HOSTS_PATH);
  }
  return path;
}

// Format: IP:port x25519 <hex> [comment]
// IPv4 example: 192.0.2.1:8080 x25519 1234abcd... ascii-chat-server
// IPv6 example: [2001:db8::1]:8080 x25519 1234abcd... ascii-chat-server
int check_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  const char *path = get_known_hosts_path();
  FILE *f = fopen(path, "r");
  if (!f)
    return 0; // File doesn't exist = first connection

  char line[2048];
  char expected_prefix[512];

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    return -1; // Invalid IP format
  }

  // Add space after IP:port for prefix matching
  snprintf(expected_prefix, sizeof(expected_prefix), "%s ", ip_with_port);

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#')
      continue; // Comment

    if (strncmp(line, expected_prefix, strlen(expected_prefix)) == 0) {
      // Found matching IP:port
      fclose(f);

      // Parse key from line
      public_key_t stored_key;
      if (parse_public_key(line + strlen(expected_prefix), &stored_key) != 0) {
        return -1; // Parse error = treat as mismatch
      }

      // Compare keys (constant-time to prevent timing attacks)
      if (sodium_memcmp(server_key, stored_key.key, 32) == 0) {
        return 1; // Match!
      } else {
        return -1; // Mismatch - MITM warning!
      }
    }
  }

  fclose(f);
  return 0; // Not found = first connection
}

int add_known_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  const char *path = get_known_hosts_path();

  // Create directory if needed
  char *dir = strdup(path);
  char *last_slash = strrchr(dir, '/');
  if (last_slash) {
    *last_slash = '\0';
    mkdir(dir, 0700);
  }
  free(dir);

  // Append to file
  FILE *f = fopen(path, "a");
  if (!f)
    return -1;

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    fclose(f);
    return -1; // Invalid IP format
  }

  // Convert key to hex for storage
  char hex[65];
  for (int i = 0; i < 32; i++) {
    snprintf(hex + i * 2, 3, "%02x", server_key[i]);
  }

  fprintf(f, "%s x25519 %s ascii-chat-server\n", ip_with_port, hex);
  fclose(f);

  return 0;
}

int remove_known_host(const char *server_ip, uint16_t port) {
  const char *path = get_known_hosts_path();
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    fclose(f);
    return -1; // Invalid IP format
  }

  // Read all lines into memory
  char **lines = NULL;
  size_t num_lines = 0;
  char line[2048];

  char expected_prefix[512];
  snprintf(expected_prefix, sizeof(expected_prefix), "%s ", ip_with_port);

  while (fgets(line, sizeof(line), f)) {
    // Skip lines that match this IP:port
    if (strncmp(line, expected_prefix, strlen(expected_prefix)) != 0) {
      // Keep this line
      char **new_lines = (char **)realloc(lines, (num_lines + 1) * sizeof(char *));
      if (new_lines) {
        lines = new_lines;
        lines[num_lines] = strdup(line);
        num_lines++;
      }
    }
  }
  fclose(f);

  // Write back the filtered lines
  f = fopen(path, "w");
  if (!f) {
    // Cleanup on error
    for (size_t i = 0; i < num_lines; i++) {
      free(lines[i]);
    }
    free(lines);
    return -1;
  }

  for (size_t i = 0; i < num_lines; i++) {
    fputs(lines[i], f);
    free(lines[i]);
  }
  free(lines);
  fclose(f);

  return 0;
}

// Compute SHA256 fingerprint of key for display
void compute_key_fingerprint(const uint8_t key[32], char fingerprint[65]) {
  uint8_t hash[32];
  crypto_hash_sha256(hash, key, 32);

  for (int i = 0; i < 32; i++) {
    snprintf(fingerprint + i * 2, 3, "%02x", hash[i]);
  }
  fingerprint[64] = '\0';
}

// Interactive prompt for unknown host - returns true if user wants to add, false to abort
bool prompt_unknown_host(const char *server_ip, uint16_t port, const uint8_t server_key[32]) {
  char fingerprint[65];
  compute_key_fingerprint(server_key, fingerprint);

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    // Fallback to basic format if error
    snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  fprintf(stderr, "\n");
  fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  fprintf(stderr, "@    WARNING: REMOTE HOST IDENTIFICATION NOT KNOWN!      @\n");
  fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "The authenticity of host '%s' can't be established.\n", ip_with_port);
  fprintf(stderr, "Ed25519 key fingerprint is SHA256:%s\n", fingerprint);
  fprintf(stderr, "\n");
  fprintf(stderr, "Are you sure you want to continue connecting (yes/no)? ");
  fflush(stderr);

  char response[10];
  if (fgets(response, sizeof(response), stdin) == NULL) {
    return false;
  }

  // Accept "yes" or "y" (case insensitive)
  if (strncasecmp(response, "yes", 3) == 0 || strncasecmp(response, "y", 1) == 0) {
    fprintf(stderr, "Warning: Permanently added '%s' to the list of known hosts.\n", ip_with_port);
    fprintf(stderr, "\n");
    return true;
  }

  fprintf(stderr, "Connection aborted by user.\n");
  return false;
}

// Display MITM warning with key comparison and removal instructions
void display_mitm_warning(const char *server_ip, uint16_t port, const uint8_t expected_key[32],
                          const uint8_t received_key[32]) {
  char expected_fp[65], received_fp[65];
  compute_key_fingerprint(expected_key, expected_fp);
  compute_key_fingerprint(received_key, received_fp);

  const char *known_hosts_path = get_known_hosts_path();

  // Format IP:port with proper bracket notation for IPv6
  char ip_with_port[512];
  if (format_ip_with_port(server_ip, port, ip_with_port, sizeof(ip_with_port)) != 0) {
    // Fallback to basic format if error
    snprintf(ip_with_port, sizeof(ip_with_port), "%s:%u", server_ip, port);
  }

  fprintf(stderr, "\n");
  fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  fprintf(stderr, "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n");
  fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\n");
  fprintf(stderr, "Someone could be eavesdropping on you right now (man-in-the-middle attack)!\n");
  fprintf(stderr, "It is also possible that the host key has just been changed.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "The fingerprint for the Ed25519 key sent by the remote host is:\n");
  fprintf(stderr, "SHA256:%s\n", received_fp);
  fprintf(stderr, "\n");
  fprintf(stderr, "Expected fingerprint:\n");
  fprintf(stderr, "SHA256:%s\n", expected_fp);
  fprintf(stderr, "\n");
  fprintf(stderr, "Please contact your system administrator.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Add correct host key in %s to get rid of this message.\n", known_hosts_path);
  fprintf(stderr, "Offending key for IP address %s was found at:\n", ip_with_port);
  fprintf(stderr, "%s\n", known_hosts_path);
  fprintf(stderr, "\n");
  fprintf(stderr, "To update the key, run:\n");
  fprintf(stderr, "  sed -i '' '/%s /d' %s\n", ip_with_port, known_hosts_path);
  fprintf(stderr, "\n");
  fprintf(stderr, "Host key verification failed.\n");
}
