#include "known_hosts.h"
#include "common.h"
#include "keys.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

#define KNOWN_HOSTS_PATH "~/.ascii-chat/known_hosts"

static char *expand_path(const char *path) {
  if (path[0] == '~') {
    const char *home = getenv("HOME");
    if (!home) {
      // On Windows, try USERPROFILE
      home = getenv("USERPROFILE");
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

// Format: hostname:port ssh-ed25519 <base64> [comment]
int check_known_host(const char *hostname, uint16_t port, const uint8_t server_key[32]) {
  const char *path = get_known_hosts_path();
  FILE *f = fopen(path, "r");
  if (!f)
    return 0; // File doesn't exist = first connection

  char line[2048];
  char expected_prefix[512];
  snprintf(expected_prefix, sizeof(expected_prefix), "%s:%u ", hostname, port);

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#')
      continue; // Comment

    if (strncmp(line, expected_prefix, strlen(expected_prefix)) == 0) {
      // Found matching hostname:port
      fclose(f);

      // Parse key from line
      public_key_t stored_key;
      if (parse_public_key(line + strlen(expected_prefix), &stored_key) != 0) {
        return -1; // Parse error = treat as mismatch
      }

      // Compare keys
      if (memcmp(server_key, stored_key.key, 32) == 0) {
        return 1; // Match!
      } else {
        return -1; // Mismatch - MITM warning!
      }
    }
  }

  fclose(f);
  return 0; // Not found = first connection
}

int add_known_host(const char *hostname, uint16_t port, const uint8_t server_key[32]) {
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

  // Convert key to hex for storage
  char hex[65];
  for (int i = 0; i < 32; i++) {
    snprintf(hex + i * 2, 3, "%02x", server_key[i]);
  }

  fprintf(f, "%s:%u x25519 %s ascii-chat-server\n", hostname, port, hex);
  fclose(f);

  return 0;
}

int remove_known_host(const char *hostname, uint16_t port) {
  const char *path = get_known_hosts_path();
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  // Read all lines into memory
  char **lines = NULL;
  size_t num_lines = 0;
  char line[2048];

  while (fgets(line, sizeof(line), f)) {
    // Skip lines that match this hostname:port
    char expected_prefix[512];
    snprintf(expected_prefix, sizeof(expected_prefix), "%s:%u ", hostname, port);

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

// Display MITM warning with key comparison
void display_mitm_warning(const uint8_t expected_key[32], const uint8_t received_key[32]) {
  printf("\n");
  printf("⚠️  WARNING: POTENTIAL MAN-IN-THE-MIDDLE ATTACK! ⚠️\n");
  printf("\n");
  printf("The server's public key has changed:\n");
  printf("\n");
  printf("Expected:  ");
  for (int i = 0; i < 32; i++) {
    printf("%02x", expected_key[i]);
  }
  printf("\n");
  printf("Received:   ");
  for (int i = 0; i < 32; i++) {
    printf("%02x", received_key[i]);
  }
  printf("\n");
  printf("\n");
  printf("This could mean:\n");
  printf("1. The server's key was legitimately updated\n");
  printf("2. You're being attacked by a man-in-the-middle\n");
  printf("\n");
  printf("If you trust this change, you can update the known_hosts file:\n");
  printf("  Edit ~/.ascii-chat/known_hosts to remove the old key\n");
  printf("  The new key will be added automatically on next connection\n");
  printf("\n");
  printf("Connection aborted for security.\n");
}
