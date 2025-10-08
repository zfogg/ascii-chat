#include "keys.h"
#include "handshake.h"
#include "http_client.h"
#include "gpg.h"
#include "ssh_agent.h"
#include "common.h"
#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <conio.h>
#include <sys/stat.h>
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#define SAFE_UNLINK _unlink
#else
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#define SAFE_UNLINK unlink
#include <errno.h>
#endif

// Forward declarations
static int prompt_with_askpass(const char *askpass_program, const char *prompt, char *passphrase, size_t max_len);
static int prompt_with_pinentry(char *passphrase, size_t max_len);

// Base64 decode SSH key blob
static int base64_decode_ssh_key(const char *base64, size_t base64_len, uint8_t **blob_out, size_t *blob_len) {
  // Allocate max possible size
  SAFE_MALLOC(*blob_out, base64_len, uint8_t *);

  const char *end;
  int result = sodium_base642bin(*blob_out, base64_len, base64, base64_len,
                                 NULL, // ignore chars
                                 blob_len, &end, sodium_base64_VARIANT_ORIGINAL);

  if (result != 0) {
    free(*blob_out);
    return -1;
  }

  return 0;
}

// Parse SSH Ed25519 public key from "ssh-ed25519 AAAAC3..." format
static int parse_ssh_ed25519_line(const char *line, uint8_t ed25519_pk[32]) {
  // Find "ssh-ed25519 "
  const char *type_start = strstr(line, "ssh-ed25519");
  if (!type_start)
    return -1;

  // Skip to base64 part
  const char *base64_start = type_start + 11; // strlen("ssh-ed25519")
  while (*base64_start == ' ' || *base64_start == '\t')
    base64_start++;

  // Find end of base64 (space, newline, or end of string)
  const char *base64_end = base64_start;
  while (*base64_end && *base64_end != ' ' && *base64_end != '\t' && *base64_end != '\n' && *base64_end != '\r') {
    base64_end++;
  }

  size_t base64_len = base64_end - base64_start;

  // Base64 decode
  uint8_t *blob;
  size_t blob_len;
  if (base64_decode_ssh_key(base64_start, base64_len, &blob, &blob_len) != 0) {
    return -1;
  }

  // Parse SSH key blob structure:
  // [4 bytes: length of "ssh-ed25519"]
  // [11 bytes: "ssh-ed25519"]
  // [4 bytes: length of public key (32)]
  // [32 bytes: Ed25519 public key]

  if (blob_len < 4 + 11 + 4 + 32) {
    free(blob);
    return -1;
  }

  // Extract Ed25519 public key (last 32 bytes)
  memcpy(ed25519_pk, blob + blob_len - 32, 32);
  free(blob);

  return 0;
}

// Decode hex string to binary
int hex_decode(const char *hex, uint8_t *output, size_t output_len) {
  if (!hex || !output) {
    return -1;
  }

  size_t hex_len = strlen(hex);
  if (hex_len != output_len * 2) {
    return -1;
  }

  for (size_t i = 0; i < output_len; i++) {
    // Check bounds before accessing
    if (i * 2 + 1 >= hex_len) {
      return -1;
    }

    char hex_byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char *endptr;
    unsigned long val = strtoul(hex_byte, &endptr, 16);
    if (*endptr != '\0' || val > 255) {
      return -1;
    }
    output[i] = (uint8_t)val;
  }

  return 0;
}

// Parse public key from any format (SSH, GPG, X25519, GitHub, etc.)
int parse_public_key(const char *input, public_key_t *key_out) {
  if (!input || !key_out) {
    return -1;
  }

  memset(key_out, 0, sizeof(public_key_t));

  // SSH Ed25519
  if (strncmp(input, "ssh-ed25519 ", 12) == 0) {
    uint8_t ed25519_pk[32];
    if (parse_ssh_ed25519_line(input, ed25519_pk) != 0) {
      return -1;
    }
    memcpy(key_out->key, ed25519_pk, 32);
    key_out->type = KEY_TYPE_ED25519;

    // Extract comment if present
    const char *comment_start = strstr(input, " ");
    if (comment_start) {
      comment_start = strstr(comment_start + 1, " ");
      if (comment_start) {
        comment_start++; // Skip the space
        strncpy(key_out->comment, comment_start, sizeof(key_out->comment) - 1);
        key_out->comment[sizeof(key_out->comment) - 1] = '\0';
      }
    }
    return 0;
  }

  if (strncmp(input, "github:", 7) == 0) {
    const char *username = input + 7;
    char **keys = NULL;
    size_t num_keys = 0;
    bool explicit_ssh = false;
    bool explicit_gpg = false;
    // Check for explicit .ssh or .gpg suffix
    const char *dot = strrchr(username, '.');
    char username_buf[256];
    if (dot) {
      if (strcmp(dot, ".ssh") == 0) {
        explicit_ssh = true;
        size_t len = dot - username;
        if (len >= sizeof(username_buf))
          len = sizeof(username_buf) - 1;
        strncpy(username_buf, username, len);
        username_buf[len] = '\0';
        username = username_buf;
      } else if (strcmp(dot, ".gpg") == 0) {
        explicit_gpg = true;
        size_t len = dot - username;
        if (len >= sizeof(username_buf))
          len = sizeof(username_buf) - 1;
        strncpy(username_buf, username, len);
        username_buf[len] = '\0';
        username = username_buf;
      }
    }

    int fetch_result;
    bool using_gpg = false;

    if (explicit_gpg) {
      // Explicit GPG mode
      fetch_result = fetch_github_keys(username, &keys, &num_keys, true);
      using_gpg = true;
      if (fetch_result != 0 || num_keys == 0) {
        log_error("No GPG keys found for GitHub user: %s", username);
        return -1;
      }
    } else if (explicit_ssh) {
      // Explicit SSH mode
      fetch_result = fetch_github_keys(username, &keys, &num_keys, false);
      using_gpg = false;
      if (fetch_result != 0 || num_keys == 0) {
        log_error("No SSH keys found for GitHub user: %s", username);
        return -1;
      }
    } else {
      // Auto mode: try SSH first
      log_info("Fetching SSH keys for GitHub user: %s", username);
      fetch_result = fetch_github_keys(username, &keys, &num_keys, false);

      if (fetch_result == 0 && num_keys > 0) {
        // SSH keys found
        using_gpg = false;
        log_info("Found %zu SSH key(s) for GitHub user: %s", num_keys, username);
      } else {
        // SSH failed or returned 0 keys, try GPG
        log_info("No SSH keys found, trying GPG keys for GitHub user: %s", username);
        fetch_result = fetch_github_keys(username, &keys, &num_keys, true);
        using_gpg = true;

        if (fetch_result != 0 || num_keys == 0) {
          log_error("No SSH or GPG keys found for GitHub user: %s", username);
          return -1;
        }
        log_info("Found %zu GPG key(s) for GitHub user: %s", num_keys, username);
      }
    }

    // Use first key
    int result;
    if (using_gpg) {
      result = parse_gpg_key(keys[0], key_out);
    } else {
      result = parse_public_key(keys[0], key_out);
    }

    // Free the keys
    for (size_t i = 0; i < num_keys; i++) {
      free(keys[i]);
    }
    free(keys);

    return result;
  }

  if (strncmp(input, "gitlab:", 7) == 0) {
    const char *username = input + 7;
    char **keys = NULL;
    size_t num_keys = 0;
    bool explicit_ssh = false;
    bool explicit_gpg = false;
    // Check for explicit .ssh or .gpg suffix
    const char *dot = strrchr(username, '.');
    char username_buf[256];
    if (dot) {
      if (strcmp(dot, ".ssh") == 0) {
        explicit_ssh = true;
        size_t len = dot - username;
        if (len >= sizeof(username_buf))
          len = sizeof(username_buf) - 1;
        strncpy(username_buf, username, len);
        username_buf[len] = '\0';
        username = username_buf;
      } else if (strcmp(dot, ".gpg") == 0) {
        explicit_gpg = true;
        size_t len = dot - username;
        if (len >= sizeof(username_buf))
          len = sizeof(username_buf) - 1;
        strncpy(username_buf, username, len);
        username_buf[len] = '\0';
        username = username_buf;
      }
    }

    int fetch_result;
    bool using_gpg = false;

    if (explicit_gpg) {
      // Explicit GPG mode
      fetch_result = fetch_gitlab_keys(username, &keys, &num_keys, true);
      using_gpg = true;
      if (fetch_result != 0 || num_keys == 0) {
        log_error("No GPG keys found for GitLab user: %s", username);
        return -1;
      }
    } else if (explicit_ssh) {
      // Explicit SSH mode
      fetch_result = fetch_gitlab_keys(username, &keys, &num_keys, false);
      using_gpg = false;
      if (fetch_result != 0 || num_keys == 0) {
        log_error("No SSH keys found for GitLab user: %s", username);
        return -1;
      }
    } else {
      // Auto mode: try SSH first
      log_info("Fetching SSH keys for GitLab user: %s", username);
      fetch_result = fetch_gitlab_keys(username, &keys, &num_keys, false);

      if (fetch_result == 0 && num_keys > 0) {
        // SSH keys found
        using_gpg = false;
        log_info("Found %zu SSH key(s) for GitLab user: %s", num_keys, username);
      } else {
        // SSH failed or returned 0 keys, try GPG
        log_info("No SSH keys found, trying GPG keys for GitLab user: %s", username);
        fetch_result = fetch_gitlab_keys(username, &keys, &num_keys, true);
        using_gpg = true;

        if (fetch_result != 0 || num_keys == 0) {
          log_error("No SSH or GPG keys found for GitLab user: %s", username);
          return -1;
        }
        log_info("Found %zu GPG key(s) for GitLab user: %s", num_keys, username);
      }
    }

    // Use first key
    int result;
    if (using_gpg) {
      result = parse_gpg_key(keys[0], key_out);
    } else {
      result = parse_public_key(keys[0], key_out);
    }

    // Free the keys
    for (size_t i = 0; i < num_keys; i++) {
      free(keys[i]);
    }
    free(keys);

    return result;
  }

  if (strncmp(input, "gpg:", 4) == 0) {
    const char *key_id = input + 4;

    // Extract public key from GPG keyring using gpg_get_public_key
    uint8_t public_key[32];
    if (gpg_get_public_key(key_id, public_key, NULL) != 0) {
      log_error("Failed to get public key for GPG key ID: %s", key_id);
      log_error("Make sure the key exists in your GPG keyring:");
      log_error("  gpg --list-keys");
      return -1;
    }

    // Store the extracted public key
    memcpy(key_out->key, public_key, 32);
    key_out->type = KEY_TYPE_ED25519;
    snprintf(key_out->comment, sizeof(key_out->comment), "gpg:%s", key_id);

    log_info("Loaded GPG public key: %s", key_id);
    return 0;
  }

  // Check for "x25519 <hex>" or "ed25519 <hex>" format (used in known_hosts)
  if (strncmp(input, "x25519 ", 7) == 0 || strncmp(input, "ed25519 ", 8) == 0) {
    const char *hex_start = strchr(input, ' ');
    if (hex_start) {
      hex_start++; // Skip the space
      // Extract hex part (64 hex characters)
      char hex[65];
      size_t i = 0;
      while (i < 64 && hex_start[i] && !isspace((unsigned char)hex_start[i])) {
        hex[i] = hex_start[i];
        i++;
      }
      if (i == 64) {
        hex[64] = '\0';
        if (hex_decode(hex, key_out->key, 32) == 0) {
          key_out->type = strncmp(input, "x25519", 6) == 0 ? KEY_TYPE_X25519 : KEY_TYPE_ED25519;
          // Extract comment if present
          const char *comment_start = hex_start + 64;
          while (*comment_start && isspace((unsigned char)*comment_start))
            comment_start++;
          if (*comment_start) {
            SAFE_STRNCPY(key_out->comment, comment_start, sizeof(key_out->comment));
            // Remove trailing newline/whitespace
            size_t len = strlen(key_out->comment);
            while (len > 0 && isspace((unsigned char)key_out->comment[len - 1]))
              key_out->comment[--len] = '\0';
          }
          return 0;
        }
      }
    }
  }

  if (strlen(input) == 64) {
    // Raw hex (X25519 public key)
    if (hex_decode(input, key_out->key, 32) == 0) {
      key_out->type = KEY_TYPE_X25519;
      return 0;
    }
    return -1;
  }

  // Try as file path - read it
  FILE *f = fopen(input, "r");
  if (f != NULL) {
    char line[2048];
    if (fgets(line, sizeof(line), f)) {
      fclose(f);
      return parse_public_key(line, key_out);
    }
    fclose(f);
  }

  log_error("Unknown public key format: %s", input);
  return -1;
}

// Convert public key to X25519 (only works for Ed25519 and X25519 types)
int public_key_to_x25519(const public_key_t *key, uint8_t x25519_pk[32]) {
  switch (key->type) {
  case KEY_TYPE_ED25519:
    return crypto_sign_ed25519_pk_to_curve25519(x25519_pk, key->key);

  case KEY_TYPE_X25519:
    memcpy(x25519_pk, key->key, 32);
    return 0;

  case KEY_TYPE_GPG:
    // GPG keys are already derived to X25519
    memcpy(x25519_pk, key->key, 32);
    return 0;

  default:
    log_error("Unknown key type: %d", key->type);
    return -1;
  }
}

// Prompt for SSH key passphrase using secure methods
static int prompt_ssh_passphrase(char *passphrase, size_t max_len) {
  // Try SSH_ASKPASS first (like SSH does)
  const char *ssh_askpass = getenv("SSH_ASKPASS");
  if (ssh_askpass && strlen(ssh_askpass) > 0) {
    fprintf(stderr, "\n[Passphrase] Using SSH_ASKPASS for passphrase input\n");
    return prompt_with_askpass(ssh_askpass, "SSH key passphrase:", passphrase, max_len);
  }

  // Try DISPLAY for GUI environments (like pinentry)
  const char *display = getenv("DISPLAY");
  if (display && strlen(display) > 0) {
    fprintf(stderr, "\n[Passphrase] GUI environment detected, trying pinentry\n");
    return prompt_with_pinentry(passphrase, max_len);
  }

  // Fallback to terminal input (less secure)
  fprintf(stderr, "\n");
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "SSH KEY PASSPHRASE REQUIRED\n");
  fprintf(stderr, "========================================\n");
  fprintf(stderr, "SSH key is encrypted. Please enter the passphrase:\n");
  fprintf(stderr, "> ");

// Disable echo for security
#ifdef _WIN32
  // Windows doesn't have termios, use _getch
  int i = 0;
  int ch;
  while (i < (int)(max_len - 1) && (ch = _getch()) != '\r' && ch != '\n') {
    if (ch == '\b' && i > 0) {
      i--;
      printf("\b \b");
    } else if (ch >= 32 && ch <= 126) {
      passphrase[i++] = ch;
      printf("*");
    }
  }
  passphrase[i] = '\0';
  printf("\n");
#else
  // Unix/Linux: disable echo
  struct termios old_termios, new_termios;
  if (tcgetattr(STDIN_FILENO, &old_termios) == 0) {
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
      if (fgets(passphrase, max_len, stdin) == NULL) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
        fprintf(stderr, "\nERROR: Failed to read passphrase\n");
        return -1;
      }
      tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    } else {
      // Fallback to normal input
      if (fgets(passphrase, max_len, stdin) == NULL) {
        fprintf(stderr, "\nERROR: Failed to read passphrase\n");
        return -1;
      }
    }
  } else {
    // Fallback to normal input
    if (fgets(passphrase, max_len, stdin) == NULL) {
      fprintf(stderr, "\nERROR: Failed to read passphrase\n");
      return -1;
    }
  }

  size_t len = strlen(passphrase);
  if (len > 0 && passphrase[len - 1] == '\n') {
    passphrase[len - 1] = '\0';
  }

  fprintf(stderr, "\n[Passphrase] Passphrase received\n");
#endif

  fprintf(stderr, "========================================\n\n");
  return 0;
}

// Use SSH_ASKPASS for secure passphrase input
static int prompt_with_askpass(const char *askpass_program, const char *prompt, char *passphrase, size_t max_len) {
  char command[1024];
  snprintf(command, sizeof(command), "%s \"%s\"", askpass_program, prompt);

  log_debug("Running SSH_ASKPASS: %s", command);

  FILE *fp = SAFE_POPEN(command, "r");
  if (!fp) {
    log_error("Failed to run SSH_ASKPASS program: %s", askpass_program);
    return -1;
  }

  if (fgets(passphrase, max_len, fp) == NULL) {
    log_error("SSH_ASKPASS program returned no output");
    SAFE_PCLOSE(fp);
    return -1;
  }

  SAFE_PCLOSE(fp);

  // Remove newline
  size_t len = strlen(passphrase);
  if (len > 0 && passphrase[len - 1] == '\n') {
    passphrase[len - 1] = '\0';
  }

  log_info("SSH_ASKPASS returned passphrase");
  return 0;
}

// Use pinentry for secure passphrase input (like GPG)
static int prompt_with_pinentry(char *passphrase, size_t max_len) {
  // Try to find pinentry program
  const char *pinentry_programs[] = {"pinentry", "pinentry-gtk-2", "pinentry-qt", "pinentry-curses", NULL};

  for (int i = 0; pinentry_programs[i] != NULL; i++) {
    char command[1024];
    snprintf(command, sizeof(command), "echo 'SETPROMPT SSH key passphrase:' | %s", pinentry_programs[i]);

    log_debug("Trying pinentry: %s", pinentry_programs[i]);

    FILE *fp = SAFE_POPEN(command, "r");
    if (fp) {
      char line[1024];
      bool got_passphrase = false;

      while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "D ", 2) == 0) {
          // Got the passphrase
          strncpy(passphrase, line + 2, max_len - 1);
          passphrase[max_len - 1] = '\0';

          // Remove newline
          size_t len = strlen(passphrase);
          if (len > 0 && passphrase[len - 1] == '\n') {
            passphrase[len - 1] = '\0';
          }

          got_passphrase = true;
          break;
        }
      }

      SAFE_PCLOSE(fp);

      if (got_passphrase) {
        log_info("pinentry returned passphrase");
        return 0;
      }
    }
  }

  log_error("No pinentry program found or failed to get passphrase");
  return -1;
}

// Check if SSH agent has a specific Ed25519 public key
// Returns: true if the key is in the agent, false otherwise
static bool ssh_agent_has_specific_key(const uint8_t ed25519_public_key[32]) {
  // Check if SSH agent is running
  const char *ssh_auth_sock = getenv("SSH_AUTH_SOCK");
  if (!ssh_auth_sock) {
    fprintf(stderr, "ssh_agent_has_specific_key: SSH_AUTH_SOCK not set\n");
    return false;
  }

  fprintf(stderr, "ssh_agent_has_specific_key: Checking SSH agent for specific key...\n");

  // List all keys in the agent
  FILE *fp = SAFE_POPEN("ssh-add -L 2>/dev/null", "r");
  if (!fp) {
    fprintf(stderr, "ssh_agent_has_specific_key: Failed to run ssh-add -L\n");
    return false;
  }

  char line[2048];
  bool found_match = false;

  while (fgets(line, sizeof(line), fp)) {
    // Look for Ed25519 keys
    if (!strstr(line, "ssh-ed25519")) {
      continue;
    }

    // Parse the key: "ssh-ed25519 <base64> comment"
    char *base64_start = strstr(line, "ssh-ed25519");
    if (!base64_start) {
      continue;
    }
    base64_start += 11; // Skip "ssh-ed25519"

    // Skip whitespace
    while (*base64_start == ' ' || *base64_start == '\t') {
      base64_start++;
    }

    // Find the end of base64 (space, newline, or end of string)
    char *base64_end = base64_start;
    while (*base64_end && *base64_end != ' ' && *base64_end != '\t' && *base64_end != '\n' && *base64_end != '\r') {
      base64_end++;
    }

    // Extract base64 string
    size_t base64_len = base64_end - base64_start;
    if (base64_len == 0 || base64_len > 1024) {
      continue;
    }

    char base64_buf[1025];
    memcpy(base64_buf, base64_start, base64_len);
    base64_buf[base64_len] = '\0';

    // Decode the public key
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    if (base64_decode_ssh_key(base64_buf, base64_len, &decoded, &decoded_len) != 0) {
      continue;
    }

    // SSH public key format: [4 bytes type_len]["ssh-ed25519"][4 bytes key_len][32 bytes key]
    // We need to extract the 32-byte key at the end
    if (decoded_len >= 51) {
      // The key should be the last 32 bytes of the decoded blob
      uint8_t *agent_key = decoded + decoded_len - 32;

      // Compare with our target key
      if (memcmp(agent_key, ed25519_public_key, 32) == 0) {
        fprintf(stderr, "ssh_agent_has_specific_key: MATCH FOUND in SSH agent!\n");
        found_match = true;
        free(decoded);
        break;
      }
    }

    free(decoded);
  }

  SAFE_PCLOSE(fp);

  if (!found_match) {
    fprintf(stderr, "ssh_agent_has_specific_key: Key NOT found in SSH agent\n");
  }

  return found_match;
}

// Decrypt SSH key using external tools (ssh-keygen)
static int decrypt_key_with_external_tool(const char *key_path, const char *passphrase, char *temp_key_path) {
// Create a temporary file for the decrypted key
#ifdef _WIN32
  char temp_dir[512];
  const char *temp_env = getenv("TEMP");
  if (!temp_env)
    temp_env = getenv("TMP");
  if (!temp_env)
    temp_env = "C:\\temp";
  snprintf(temp_dir, sizeof(temp_dir), "%s\\ascii-chat-temp-key-%d", temp_env, getpid());
#else
  char temp_dir[512];
  snprintf(temp_dir, sizeof(temp_dir), "/tmp/ascii-chat-temp-key-%d", getpid());
#endif

  // Copy encrypted key to temp location first (ssh-keygen -p modifies in place)
  char copy_command[1024];
  snprintf(copy_command, sizeof(copy_command), "cp \"%s\" \"%s\"", key_path, temp_dir);

  int copy_result = system(copy_command);
  if (copy_result != 0) {
    log_error("Failed to copy key to temp location (exit code: %d)", copy_result);
    return -1;
  }

  // Use ssh-keygen to decrypt the temp copy (removes passphrase in place)
  char command[1024];
  snprintf(command, sizeof(command), "ssh-keygen -p -N \"\" -P \"%s\" -f \"%s\" 2>/dev/null", passphrase, temp_dir);

  int result = system(command);
  if (result != 0) {
    fprintf(stderr, "[Decrypt] ERROR: ssh-keygen failed (exit code: %d)\n", result);
    unlink(temp_dir); // Clean up temp file on failure
    return -1;
  }

  // Copy the temp path to output
  strncpy(temp_key_path, temp_dir, 511);
  temp_key_path[511] = '\0';

  return 0;
}

// Parse SSH private key from file
//
// CURRENT SUPPORT: Ed25519 only
// FUTURE: RSA and ECDSA support would require:
//   1. Update private_key_t to store variable-length public keys (malloc'd buffer)
//   2. Store key type string (ssh-rsa, ecdsa-sha2-nistp256, etc.)
//   3. Update ed25519_sign_message() to build correct ssh-agent request per key type
//   4. Update signature parsing to handle variable-length signatures
//   5. Add signature verification for RSA/ECDSA (requires OpenSSL or ssh-keygen -Y verify)
//   6. Update protocol to support variable-length authenticated handshake packets
//
// See: https://datatracker.ietf.org/doc/html/rfc4253#section-6.6 for SSH key formats
//
asciichat_error_status_t parse_private_key(const char *path, private_key_t *key_out) {
  fprintf(stderr, "parse_private_key: Opening %s\n", path);
  memset(key_out, 0, sizeof(private_key_t));

  // Reject GitHub/GitLab keys - they only provide public keys, not private keys
  if (strncmp(path, "github:", 7) == 0 || strncmp(path, "gitlab:", 7) == 0) {
    log_error("GitHub/GitLab keys cannot be used for private key authentication");
    log_error("GitHub/GitLab only provide public keys, not private keys.");
    log_error("");
    log_error("For private key authentication, use:");
    log_error("  /path/to/ssh/key    (SSH private key file)");
    log_error("  gpg:KEYID           (GPG key via gpg-agent)");
    log_error("");
    log_error("GitHub/GitLab keys work with --client-keys and --server-keys");
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Check for gpg:keyid format
  if (strncmp(path, "gpg:", 4) == 0) {
    const char *key_id = path + 4;
    fprintf(stderr, "parse_private_key: Detected GPG key ID: %s\n", key_id);

    // Check if gpg-agent is available
    if (!gpg_agent_is_available()) {
      log_error("GPG agent is not available. Please ensure gpg-agent is running.");
      log_error("You can start it with: gpgconf --launch gpg-agent");
      log_error("");
      log_error("GPG agent is REQUIRED for 'gpg:' key format.");
      log_error("Install GPG:");
      log_error("  Ubuntu/Debian: sudo apt-get install gnupg gpg-agent");
      log_error("  Fedora/RHEL:   sudo dnf install gnupg2");
      log_error("  macOS:         brew install gnupg");
      log_error("  Arch:          sudo pacman -S gnupg");
      return ASCIICHAT_ERROR_CRYPTO_KEY;
    }

    // Extract public key and keygrip from GPG keyring
    uint8_t public_key[32];
    char keygrip[64];
    if (gpg_get_public_key(key_id, public_key, keygrip) != 0) {
      log_error("Failed to get public key for GPG key ID: %s", key_id);
      return ASCIICHAT_ERROR_CRYPTO_KEY;
    }

    fprintf(stderr, "parse_private_key: Got GPG public key and keygrip: %s\n", keygrip);
    log_info("Using GPG key %s via gpg-agent (keygrip: %s)", key_id, keygrip);

    // Set up the private_key_t structure for GPG agent mode
    key_out->type = KEY_TYPE_ED25519;
    key_out->use_ssh_agent = false;
    key_out->use_gpg_agent = true;
    memcpy(key_out->public_key, public_key, 32);
    SAFE_STRNCPY(key_out->gpg_keygrip, keygrip, sizeof(key_out->gpg_keygrip) - 1);
    SAFE_STRNCPY(key_out->key_comment, key_id, sizeof(key_out->key_comment) - 1);

    // Zero out the key union (we don't have private key bytes in agent mode)
    memset(&key_out->key, 0, sizeof(key_out->key));

    log_info("GPG agent mode: Will use gpg-agent for signing");
    return ASCIICHAT_OK;
  }

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "parse_private_key: Failed to open file: %s\n", path);
    log_error("Cannot open SSH key file: %s", path);
    log_error("Error: %s", strerror(errno));
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  fprintf(stderr, "parse_private_key: File opened successfully\n");

  char line[2048];
  bool in_private_key = false;
  char base64_data[8192] = {0}; // Buffer for base64 data
  size_t base64_len = 0;

  while (fgets(line, sizeof(line), f)) {
    // Remove newline
    line[strcspn(line, "\r\n")] = '\0';

    if (strstr(line, "BEGIN OPENSSH PRIVATE KEY") || strstr(line, "BEGIN PRIVATE KEY")) {
      in_private_key = true;
      continue;
    }

    if (strstr(line, "END OPENSSH PRIVATE KEY") || strstr(line, "END PRIVATE KEY")) {
      break;
    }

    if (in_private_key) {
      // Accumulate base64 data
      size_t line_len = strlen(line);
      if (base64_len + line_len < sizeof(base64_data)) {
        strcat(base64_data, line);
        base64_len += line_len;
      }
    }
  }

  fclose(f);

  if (base64_len == 0) {
    fprintf(stderr, "parse_private_key: No base64 data found\n");
    log_error("No private key data found in file: %s", path);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  fprintf(stderr, "parse_private_key: Read %zu bytes of base64 data\n", base64_len);

  // Decode base64 data
  uint8_t *blob = NULL;
  size_t blob_len = 0;
  fprintf(stderr, "parse_private_key: Decoding base64...\n");
  if (base64_decode_ssh_key(base64_data, base64_len, &blob, &blob_len) != 0) {
    fprintf(stderr, "parse_private_key: Base64 decode failed\n");
    log_error("Failed to decode base64 private key data");
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  fprintf(stderr, "parse_private_key: Decoded to %zu bytes\n", blob_len);

  // Parse OpenSSH private key format
  // Format: [4 bytes: magic] [4 bytes: ciphername] [4 bytes: kdfname] [4 bytes: kdfoptions] [4 bytes: nkeys] [4 bytes:
  // pubkey] [4 bytes: privkey] [data...]
  if (blob_len < 32) {
    fprintf(stderr, "parse_private_key: Blob too short (%zu bytes)\n", blob_len);
    log_error("Private key data too short");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  fprintf(stderr, "parse_private_key: Blob length OK (%zu bytes)\n", blob_len);

  // Check magic number (OpenSSH private key format)
  fprintf(stderr, "parse_private_key: Checking magic number...\n");
  if (memcmp(blob, "openssh-key-v1\0", 15) != 0) {
    fprintf(stderr, "parse_private_key: Magic number mismatch (not OpenSSH format)\n");
    log_error("Not an OpenSSH private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  fprintf(stderr, "parse_private_key: Magic number OK (OpenSSH format)\n");

  size_t offset = 15; // Skip magic
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Not enough data for ciphername length\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Read ciphername (should be "none" for unencrypted keys)
  uint32_t ciphername_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  fprintf(stderr, "parse_private_key: Ciphername length: %u\n", ciphername_len);

  if (offset + ciphername_len > blob_len) {
    fprintf(stderr, "parse_private_key: Not enough data for ciphername (need %u, have %zu)\n", ciphername_len,
            blob_len - offset);
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Save cipher info for later check
  bool is_encrypted = !(ciphername_len == 4 && memcmp(blob + offset, "none", 4) == 0);
  char ciphername[256] = {0};
  if (is_encrypted) {
    if (ciphername_len >= sizeof(ciphername)) {
      fprintf(stderr, "parse_private_key: Ciphername too long (%u bytes)\n", ciphername_len);
      log_error("Cipher name too long");
      free(blob);
      return ASCIICHAT_ERROR_CRYPTO_KEY;
    }
    memcpy(ciphername, blob + offset, ciphername_len);
    ciphername[ciphername_len] = '\0';
    fprintf(stderr, "parse_private_key: Encrypted key detected (cipher: %s)\n", ciphername);
  }
  offset += ciphername_len;

  // Skip KDF name and options to get to public key
  // Read kdfname
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfname length\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t kdfname_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + kdfname_len > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfname\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  offset += kdfname_len;

  // Read kdfoptions
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfoptions length\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t kdfoptions_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + kdfoptions_len > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfoptions\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  offset += kdfoptions_len;

  // Read number of keys
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at nkeys\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t nkeys = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (nkeys != 1) {
    log_error("Expected 1 key, found %u", nkeys);
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Read and extract public key BEFORE handling encryption
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at pubkey length\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t pubkey_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + pubkey_len > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at pubkey data\n");
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Extract Ed25519 public key from the public key blob
  // Format: [4 bytes type_len]["ssh-ed25519"][4 bytes key_len][32 bytes key]
  if (pubkey_len < 51) {
    fprintf(stderr, "parse_private_key: Public key too short\n");
    log_error("Public key too short");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  uint32_t key_type_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  if (key_type_len != 11 || memcmp(blob + offset + 4, "ssh-ed25519", 11) != 0) {
    // Extract key type for better error message
    char key_type[256] = {0};
    if (key_type_len < sizeof(key_type) && offset + 4 + key_type_len <= blob_len) {
      memcpy(key_type, blob + offset + 4, key_type_len);
      key_type[key_type_len] = '\0';
      fprintf(stderr, "parse_private_key: Unsupported key type: %s\n", key_type);
      log_error("Unsupported key type '%s' - only Ed25519 is currently supported", key_type);
      log_error("SSH key type detected: %s", key_type);
      log_error("RSA and ECDSA keys are not yet supported for the following reasons:");
      log_error("  1. Variable-length public keys (RSA: 256+ bytes vs Ed25519: 32 bytes)");
      log_error("  2. Variable-length signatures (RSA: 256 bytes vs Ed25519: 64 bytes)");
      log_error("  3. Signature verification requires OpenSSL (currently using libsodium)");
      log_error("  4. Protocol format assumes 128-byte authenticated handshake (32+32+64)");
      log_error("");
      log_error("To use this key, either:");
      log_error("  1. Generate an Ed25519 key: ssh-keygen -t ed25519");
      log_error("  2. Contribute RSA/ECDSA support (see lib/crypto/keys.c)");
    } else {
      fprintf(stderr, "parse_private_key: Not an Ed25519 key\n");
      log_error("Unsupported key type - only Ed25519 is currently supported");
    }
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  size_t pubkey_offset = offset + 4 + 11; // Skip type_len and "ssh-ed25519"
  uint32_t embedded_key_len = (blob[pubkey_offset] << 24) | (blob[pubkey_offset + 1] << 16) |
                              (blob[pubkey_offset + 2] << 8) | blob[pubkey_offset + 3];
  if (embedded_key_len != 32) {
    fprintf(stderr, "parse_private_key: Embedded key length is %u, expected 32\n", embedded_key_len);
    log_error("Invalid Ed25519 key length");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  uint8_t embedded_public_key[32];
  memcpy(embedded_public_key, blob + pubkey_offset + 4, 32);
  fprintf(stderr, "parse_private_key: Extracted embedded Ed25519 public key (32 bytes)\n");
  offset += pubkey_len;

  // NOW check if the key is encrypted and handle accordingly
  if (is_encrypted) {
    log_info("Encrypted private key detected (cipher: %s)", ciphername);

    // Check if THIS SPECIFIC key is in SSH agent (not just any Ed25519 key)
    bool ssh_agent_has_key = ssh_agent_has_specific_key(embedded_public_key);

    if (ssh_agent_has_key) {
      fprintf(stderr, "parse_private_key: THIS SPECIFIC key found in SSH agent!\n");
      log_info("Using SSH agent for this key (agent signing + ephemeral encryption)");

      // Mode 1: Encrypted key + SSH agent
      // Use agent for signing, ephemeral keys for encryption
      key_out->type = KEY_TYPE_ED25519;
      key_out->use_ssh_agent = true;
      memcpy(key_out->public_key, embedded_public_key, 32);
      // TODO: Extract comment from somewhere (not critical)
      SAFE_STRNCPY(key_out->key_comment, "ssh-agent", sizeof(key_out->key_comment) - 1);

      // Zero out the key union (we don't have private key bytes in agent mode)
      memset(&key_out->key, 0, sizeof(key_out->key));

      log_info("SSH agent mode: Will use agent for identity signing, ephemeral X25519 for encryption");
      free(blob);
      return ASCIICHAT_OK;
    }

    fprintf(stderr, "parse_private_key: Key not in SSH agent, will prompt for password\n");

    // Mode 2: Encrypted key + password
    // Prompt for password and decrypt key, use for both identity and encryption
    fprintf(stderr, "[Decrypt] Attempting to decrypt key file...\n");
    char passphrase[256];
    if (prompt_ssh_passphrase(passphrase, sizeof(passphrase)) != 0) {
      fprintf(stderr, "[Decrypt] ERROR: Failed to get passphrase\n");
      free(blob);
      return ASCIICHAT_ERROR_CRYPTO_KEY;
    }
    fprintf(stderr, "[Decrypt] Attempting to decrypt with external tools (ssh-keygen)...\n");

    // Use ssh-keygen to decrypt the key temporarily
    char temp_key_path[512];
    if (decrypt_key_with_external_tool(path, passphrase, temp_key_path) == 0) {
      fprintf(stderr, "[Decrypt] Successfully decrypted key, parsing...\n");
      free(blob);

      // Parse the decrypted key (it's now unencrypted so SSH agent check won't trigger)
      asciichat_error_status_t result = parse_private_key(temp_key_path, key_out);

      // Clean up temporary file
      unlink(temp_key_path);

      if (result == ASCIICHAT_OK) {
        fprintf(stderr, "[Decrypt] Successfully parsed decrypted SSH key\n");

        // Try to add key to ssh-agent so user doesn't have to enter password again
        if (ssh_agent_is_available()) {
          // Check if ssh-add command is available
          FILE *which_fp = SAFE_POPEN("which ssh-add 2>/dev/null", "r");
          char ssh_add_path[256] = {0};
          bool ssh_add_available = false;

          if (which_fp) {
            if (fgets(ssh_add_path, sizeof(ssh_add_path), which_fp) != NULL) {
              ssh_add_available = true;
            }
            SAFE_PCLOSE(which_fp);
          }

          if (!ssh_add_available) {
            log_warn("ssh-add command not found in PATH - cannot automatically add key to agent");
            log_warn("Install OpenSSH client package to enable automatic key addition:");
            log_warn("  Ubuntu/Debian: sudo apt-get install openssh-client");
            log_warn("  Fedora/RHEL:   sudo dnf install openssh-clients");
            log_warn("  macOS:         ssh-add is pre-installed");
            log_warn("  Arch:          sudo pacman -S openssh");
          } else {
            fprintf(stderr, "[SSH Agent] Adding key to ssh-agent to avoid future password prompts...\n");

            // Shell out to ssh-add with the original key path
            // Use expect/SSH_ASKPASS to provide password non-interactively
            char ssh_add_cmd[1024];

            // Create a temporary script to provide the password
            char askpass_script[512];
            snprintf(askpass_script, sizeof(askpass_script), "/tmp/ascii-chat-askpass-%d.sh", getpid());

            FILE *askpass_fp = fopen(askpass_script, "w");
            if (askpass_fp) {
              fprintf(askpass_fp, "#!/bin/sh\necho '%s'\n", passphrase);
              fclose(askpass_fp);
              chmod(askpass_script, 0700);

              // Use SSH_ASKPASS environment variable
              snprintf(ssh_add_cmd, sizeof(ssh_add_cmd), "SSH_ASKPASS='%s' SSH_ASKPASS_REQUIRE=force ssh-add '%s' 2>&1",
                       askpass_script, path);

              FILE *ssh_add_fp = SAFE_POPEN(ssh_add_cmd, "r");
              if (ssh_add_fp) {
                char output[256];
                bool added = false;
                while (fgets(output, sizeof(output), ssh_add_fp)) {
                  if (strstr(output, "Identity added")) {
                    added = true;
                    fprintf(stderr, "[SSH Agent] ✓ Key successfully added to ssh-agent\n");
                    log_info("Key added to ssh-agent - password won't be required again this session");
                  }
                }
                SAFE_PCLOSE(ssh_add_fp);

                if (!added) {
                  fprintf(stderr, "[SSH Agent] Warning: Could not add key to ssh-agent\n");
                }
              }

              // Clean up askpass script
              unlink(askpass_script);
            }
          } // Close ssh_add_available block
        }

        // Zero out passphrase after using it
        sodium_memzero(passphrase, sizeof(passphrase));
        fprintf(stderr, "\n");
        return ASCIICHAT_OK;
      } else {
        fprintf(stderr, "[Decrypt] ERROR: Failed to parse decrypted key\n");
        sodium_memzero(passphrase, sizeof(passphrase));
        return ASCIICHAT_ERROR_CRYPTO_KEY;
      }
    } else {
      fprintf(stderr, "[Decrypt] ERROR: Failed to decrypt key with external tools\n");
      fprintf(stderr, "[Decrypt] ERROR: Incorrect password or corrupted key file\n");
      fprintf(stderr, "\n");
      fprintf(stderr, "Please try one of these methods:\n");
      fprintf(stderr, "  1. Add key to SSH agent: ssh-add %s\n", path);
      fprintf(stderr, "  2. Convert to unencrypted: ssh-keygen -p -N \"\" -f %s\n", path);
      fprintf(stderr, "  3. Use an unencrypted Ed25519 key\n");
      fprintf(stderr, "\n");

      sodium_memzero(passphrase, sizeof(passphrase));
      free(blob);
      return ASCIICHAT_ERROR_CRYPTO_KEY;
    }
  }

  // For unencrypted keys, continue parsing the private key blob

  // Read private key blob
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at privkey blob length (offset=%zu, blob_len=%zu)\n", offset,
            blob_len);
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t privkey_blob_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  fprintf(stderr, "parse_private_key: Private key blob length: %u\n", privkey_blob_len);
  offset += 4;
  if (offset + privkey_blob_len > blob_len) {
    fprintf(
        stderr,
        "parse_private_key: Invalid key format at privkey blob data (offset=%zu, privkey_blob_len=%u, blob_len=%zu)\n",
        offset, privkey_blob_len, blob_len);
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Private key blob starts with check1 and check2 (8 bytes total)
  if (privkey_blob_len < 8) {
    fprintf(stderr, "parse_private_key: Private key blob too short for check values (privkey_blob_len=%u)\n",
            privkey_blob_len);
    log_error("Private key blob too short");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  fprintf(stderr, "parse_private_key: Skipping check1/check2 (8 bytes)\n");
  offset += 8; // Skip check1 and check2

  // Parse private key data
  // Format: [4 bytes keytype length]["ssh-ed25519"][4 bytes pubkey length][32 bytes pubkey][4 bytes privkey length][64
  // bytes privkey][4 bytes comment length][comment][padding]
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Cannot read keytype length (offset=%zu, blob_len=%zu)\n", offset, blob_len);
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Read the key type string length
  uint32_t priv_key_type_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  fprintf(stderr, "parse_private_key: Private key type string length: %u\n", priv_key_type_len);

  // Check if it's "ssh-ed25519"
  if (priv_key_type_len != 11 || memcmp(blob + offset + 4, "ssh-ed25519", 11) != 0) {
    fprintf(stderr, "parse_private_key: Not an Ed25519 private key (priv_key_type_len=%u)\n", priv_key_type_len);
    log_error("Not an Ed25519 private key");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Skip type string (4 bytes length + 11 bytes "ssh-ed25519")
  fprintf(stderr, "parse_private_key: Parsing private key structure...\n");
  offset += 4 + 11;

  // Read public key length
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Cannot read public key length (offset=%zu, blob_len=%zu)\n", offset, blob_len);
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t pubkey_data_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  fprintf(stderr, "parse_private_key: Public key data length in privkey blob: %u\n", pubkey_data_len);
  offset += 4;

  if (pubkey_data_len != 32) {
    fprintf(stderr, "parse_private_key: Expected 32-byte Ed25519 public key, got %u bytes\n", pubkey_data_len);
    log_error("Invalid Ed25519 public key length");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  if (offset + 32 > blob_len) {
    fprintf(stderr, "parse_private_key: Public key data too short (offset=%zu, need 32 bytes, blob_len=%zu)\n", offset,
            blob_len);
    log_error("Public key data too short");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Skip public key (we'll extract it from the private key later)
  fprintf(stderr, "parse_private_key: Skipping public key (32 bytes)\n");
  offset += 32;

  // Read private key length
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Cannot read private key length (offset=%zu, blob_len=%zu)\n", offset, blob_len);
    log_error("Invalid private key format");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }
  uint32_t privkey_data_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  fprintf(stderr, "parse_private_key: Private key data length: %u\n", privkey_data_len);
  offset += 4;

  if (privkey_data_len != 64) {
    fprintf(stderr, "parse_private_key: Expected 64-byte Ed25519 private key, got %u bytes\n", privkey_data_len);
    log_error("Invalid Ed25519 private key length");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  if (offset + 64 > blob_len) {
    fprintf(stderr, "parse_private_key: Private key data too short (offset=%zu, need 64 bytes, blob_len=%zu)\n", offset,
            blob_len);
    log_error("Private key data too short");
    free(blob);
    return ASCIICHAT_ERROR_CRYPTO_KEY;
  }

  // Extract the 64-byte Ed25519 private key (32-byte seed + 32-byte public key)
  key_out->type = KEY_TYPE_ED25519;
  key_out->use_ssh_agent = false; // Mode 3: Unencrypted key, use in-memory signing
  memcpy(key_out->key.ed25519, blob + offset, 64);

  // Extract the public key from the private key (last 32 bytes)
  memcpy(key_out->public_key, blob + offset + 32, 32);

  // Extract key comment if available
  // Comment is after the private key data
  size_t comment_offset = offset + 64;
  if (comment_offset + 4 <= blob_len) {
    uint32_t comment_len = (blob[comment_offset] << 24) | (blob[comment_offset + 1] << 16) |
                           (blob[comment_offset + 2] << 8) | blob[comment_offset + 3];
    comment_offset += 4;
    if (comment_offset + comment_len <= blob_len && comment_len < sizeof(key_out->key_comment)) {
      memcpy(key_out->key_comment, blob + comment_offset, comment_len);
      key_out->key_comment[comment_len] = '\0';
    } else {
      key_out->key_comment[0] = '\0';
    }
  } else {
    key_out->key_comment[0] = '\0';
  }

  // Clear sensitive data
  sodium_memzero(blob, blob_len);
  free(blob);

  log_info("Successfully parsed Ed25519 private key from %s (in-memory mode)", path);
  return ASCIICHAT_OK;
}

// Convert private key to X25519 for DH
int private_key_to_x25519(const private_key_t *key, uint8_t x25519_sk[32]) {
  // SSH agent mode: we don't have private key bytes, caller should use ephemeral keys
  if (key->use_ssh_agent) {
    log_error("Cannot convert SSH agent key to X25519 (no private key bytes)");
    log_error("Caller should use ephemeral X25519 keys for encryption");
    return -1;
  }

  switch (key->type) {
  case KEY_TYPE_ED25519:
    return crypto_sign_ed25519_sk_to_curve25519(x25519_sk, key->key.ed25519);

  case KEY_TYPE_X25519:
    memcpy(x25519_sk, key->key.x25519, 32);
    return 0;

  default:
    log_error("Unknown private key type: %d", key->type);
    return -1;
  }
}

/**
 * Parse Ed25519 public key from PGP armored format
 * @param gpg_key_text PGP armored public key block
 * @param key_out Output buffer for parsed key
 * @return 0 on success, -1 on error
 */
int parse_gpg_key(const char *gpg_key_text, public_key_t *key_out) {
  if (!gpg_key_text || !key_out) {
    log_error("Invalid arguments to parse_gpg_key");
    return -1;
  }

  // Find the armored block
  const char *begin = strstr(gpg_key_text, "-----BEGIN PGP PUBLIC KEY BLOCK-----");
  if (!begin) {
    log_error("No PGP BEGIN marker found");
    return -1;
  }

  const char *end_marker = strstr(begin, "-----END PGP PUBLIC KEY BLOCK-----");
  if (!end_marker) {
    log_error("No PGP END marker found");
    return -1;
  }

  // Extract base64 content (skip BEGIN line and empty line)
  const char *b64_start = begin + strlen("-----BEGIN PGP PUBLIC KEY BLOCK-----");
  while (*b64_start && (*b64_start == '\n' || *b64_start == '\r' || *b64_start == ' ')) {
    b64_start++;
  }

  // Find actual base64 content (skip empty lines and version headers)
  while (*b64_start && strncmp(b64_start, "Version:", 8) == 0) {
    // Skip version line
    while (*b64_start && *b64_start != '\n') {
      b64_start++;
    }
    if (*b64_start == '\n') {
      b64_start++;
    }
    // Skip empty line after version
    while (*b64_start && (*b64_start == '\n' || *b64_start == '\r' || *b64_start == ' ')) {
      b64_start++;
    }
  }

  // Calculate base64 length (up to checksum line)
  // Checksum line starts with '=' at beginning of line (e.g., "=EjXs")
  // But base64 padding '==' is at end of last base64 line
  const char *b64_end = b64_start;
  const char *line_start = b64_start;

  while (b64_end < end_marker) {
    if (*b64_end == '\n' || *b64_end == '\r') {
      // Move to start of next line
      b64_end++;
      while (b64_end < end_marker && (*b64_end == '\n' || *b64_end == '\r')) {
        b64_end++;
      }
      line_start = b64_end;
    } else if (*b64_end == '=' && b64_end == line_start) {
      // '=' at start of line = checksum line, stop here
      break;
    } else if (*b64_end == '-') {
      // Reached END marker
      break;
    } else {
      b64_end++;
    }
  }

  // Copy base64 data without newlines
  size_t max_b64_len = b64_end - b64_start;
  char *b64_clean = malloc(max_b64_len + 1);
  if (!b64_clean) {
    log_error("Failed to allocate memory for base64 data");
    return -1;
  }

  size_t clean_len = 0;
  for (const char *p = b64_start; p < b64_end; p++) {
    if (*p != '\n' && *p != '\r' && *p != ' ') {
      b64_clean[clean_len++] = *p;
    }
  }
  b64_clean[clean_len] = '\0';

  // Decode base64
  unsigned char decoded[8192];
  size_t decoded_len;
  const char *b64_parse_end;
  if (sodium_base642bin(decoded, sizeof(decoded), b64_clean, clean_len, NULL, &decoded_len, &b64_parse_end,
                        sodium_base64_VARIANT_ORIGINAL) != 0) {
    log_error("Failed to decode PGP base64 data");
    free(b64_clean);
    return -1;
  }
  free(b64_clean);

  log_debug("Decoded %zu bytes of PGP data", decoded_len);

  // Parse PGP packets to find Ed25519 public key
  size_t offset = 0;
  bool found_ed25519 = false;

  while (offset < decoded_len && !found_ed25519) {
    // Parse packet header
    if (offset >= decoded_len) {
      break;
    }

    uint8_t tag_byte = decoded[offset++];
    uint8_t tag;
    size_t packet_len;

    // RFC 4880: bit 6 = 0 means old format, bit 6 = 1 means new format
    if ((tag_byte & 0x40) == 0) {
      // Old format packet
      tag = (tag_byte >> 2) & 0x0F;
      uint8_t len_type = tag_byte & 0x03;

      if (len_type == 0) {
        if (offset >= decoded_len)
          break;
        packet_len = decoded[offset++];
      } else if (len_type == 1) {
        if (offset + 1 >= decoded_len)
          break;
        packet_len = (decoded[offset] << 8) | decoded[offset + 1];
        offset += 2;
      } else if (len_type == 2) {
        if (offset + 3 >= decoded_len)
          break;
        packet_len =
            (decoded[offset] << 24) | (decoded[offset + 1] << 16) | (decoded[offset + 2] << 8) | decoded[offset + 3];
        offset += 4;
      } else {
        // Indeterminate length
        log_debug("Skipping packet with indeterminate length");
        break;
      }
    } else {
      // New format packet
      tag = tag_byte & 0x3F;

      // Parse length
      if (offset >= decoded_len) {
        break;
      }
      uint8_t len_byte = decoded[offset++];
      if (len_byte < 192) {
        packet_len = len_byte;
      } else if (len_byte < 224) {
        if (offset >= decoded_len) {
          break;
        }
        packet_len = ((len_byte - 192) << 8) + decoded[offset++] + 192;
      } else {
        // Partial body length or other format - skip
        log_debug("Skipping packet with complex length encoding");
        break;
      }
    }

    log_debug("PGP packet: tag=%u, len=%zu", tag, packet_len);

    // Check if this is a public key packet (tag 6) or public subkey packet (tag 14)
    if ((tag == 6 || tag == 14) && offset + packet_len <= decoded_len) {
      // Parse public key packet
      size_t pkt_offset = offset;

      // Version (1 byte)
      if (pkt_offset >= offset + packet_len)
        goto next_packet;
      uint8_t version = decoded[pkt_offset++];

      // Creation time (4 bytes)
      if (pkt_offset + 4 > offset + packet_len)
        goto next_packet;
      pkt_offset += 4;

      // Algorithm (1 byte)
      if (pkt_offset >= offset + packet_len)
        goto next_packet;
      uint8_t algorithm = decoded[pkt_offset++];

      log_debug("Public key packet: version=%u, algorithm=%u", version, algorithm);

      // Check if Ed25519 (algorithm 22)
      if (algorithm == 22) {
        // Ed25519 keys in OpenPGP v4 include an OID (Object Identifier) field
        // Format: OID length (1 byte) + OID data (variable length) + MPI
        // The OID for Ed25519 is: 1.3.6.1.4.1.11591.15.1
        // Encoded as: 09 2b 06 01 04 01 da 47 0f 01 (length + data)

        if (pkt_offset >= offset + packet_len)
          goto next_packet;

        // Read OID length
        uint8_t oid_len = decoded[pkt_offset++];
        log_debug("Ed25519 OID length: %u", oid_len);

        // Skip OID data
        if (pkt_offset + oid_len > offset + packet_len) {
          log_error("OID extends beyond packet");
          goto next_packet;
        }
        pkt_offset += oid_len;

        // Now read the MPI
        if (pkt_offset + 2 > offset + packet_len)
          goto next_packet;

        // Read MPI bit count (2 bytes, big endian)
        uint16_t bit_count = (decoded[pkt_offset] << 8) | decoded[pkt_offset + 1];
        pkt_offset += 2;

        log_debug("Ed25519 MPI bit count: %u", bit_count);

        // Ed25519 uses 263 bits (0x40 prefix byte + 32 bytes = 33 bytes = 264 bits)
        // Some implementations use 256 bits (just the 32 bytes)
        size_t mpi_bytes = (bit_count + 7) / 8;

        if (pkt_offset + mpi_bytes > offset + packet_len) {
          log_error("Ed25519 MPI extends beyond packet");
          goto next_packet;
        }

        // Check if it has the 0x40 prefix (standard format)
        if (mpi_bytes == 33 && decoded[pkt_offset] == 0x40) {
          // Skip 0x40 prefix and copy 32 bytes
          memcpy(key_out->key, decoded + pkt_offset + 1, 32);
          key_out->type = KEY_TYPE_ED25519;
          found_ed25519 = true;
          log_info("Successfully extracted Ed25519 public key from PGP packet");
        } else if (mpi_bytes == 32) {
          // No prefix, just 32 bytes
          memcpy(key_out->key, decoded + pkt_offset, 32);
          key_out->type = KEY_TYPE_ED25519;
          found_ed25519 = true;
          log_info("Successfully extracted Ed25519 public key from PGP packet (no prefix)");
        } else {
          log_error("Unexpected Ed25519 MPI size: %zu bytes", mpi_bytes);
        }
      }
    }

  next_packet:
    offset += packet_len;
  }

  if (!found_ed25519) {
    log_error("No Ed25519 public key found in PGP data");
    return -1;
  }

  return 0;
}

// Fetch SSH keys from GitHub using BearSSL
int fetch_github_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg) {
  if (use_gpg) {
    return fetch_github_gpg_keys(username, keys_out, num_keys);
  } else {
    return fetch_github_ssh_keys(username, keys_out, num_keys);
  }
}

// Fetch SSH keys from GitLab using BearSSL
int fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg) {
  if (use_gpg) {
    return fetch_gitlab_gpg_keys(username, keys_out, num_keys);
  } else {
    return fetch_gitlab_ssh_keys(username, keys_out, num_keys);
  }
}

// Parse SSH/GPG keys from file (supports authorized_keys, known_hosts, and PGP armored formats)
int parse_keys_from_file(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  // Don't reset num_keys - append to existing keys
  char line[2048];
  char *pgp_block = NULL;
  size_t pgp_block_size = 0;
  size_t pgp_block_capacity = 0;
  bool in_pgp_block = false;

  while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
    // Check for PGP block start
    if (strstr(line, "-----BEGIN PGP PUBLIC KEY BLOCK-----")) {
      in_pgp_block = true;
      pgp_block_size = 0;
      if (!pgp_block) {
        pgp_block_capacity = 8192;
        SAFE_MALLOC(pgp_block, pgp_block_capacity, char *);
      }
      // Add BEGIN line to block
      size_t line_len = strlen(line);
      if (pgp_block_size + line_len >= pgp_block_capacity) {
        pgp_block_capacity *= 2;
        SAFE_REALLOC(pgp_block, pgp_block_capacity, char *);
      }
      memcpy(pgp_block + pgp_block_size, line, line_len);
      pgp_block_size += line_len;
      continue;
    }

    // Check for PGP block end
    if (in_pgp_block && strstr(line, "-----END PGP PUBLIC KEY BLOCK-----")) {
      // Add END line to block
      size_t line_len = strlen(line);
      if (pgp_block_size + line_len >= pgp_block_capacity) {
        pgp_block_capacity *= 2;
        SAFE_REALLOC(pgp_block, pgp_block_capacity, char *);
      }
      memcpy(pgp_block + pgp_block_size, line, line_len);
      pgp_block_size += line_len;
      pgp_block[pgp_block_size] = '\0';

      // Parse the complete PGP block
      if (parse_gpg_key(pgp_block, &keys[*num_keys]) == 0) {
        (*num_keys)++;
        log_info("Parsed GPG key from file");
      } else {
        log_warn("Failed to parse GPG key block from file");
      }

      in_pgp_block = false;
      pgp_block_size = 0;
      continue;
    }

    // If we're in a PGP block, accumulate lines
    if (in_pgp_block) {
      size_t line_len = strlen(line);
      if (pgp_block_size + line_len >= pgp_block_capacity) {
        pgp_block_capacity *= 2;
        SAFE_REALLOC(pgp_block, pgp_block_capacity, char *);
      }
      memcpy(pgp_block + pgp_block_size, line, line_len);
      pgp_block_size += line_len;
      continue;
    }

    // Not in PGP block - parse as SSH key
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
      continue;

    // Strip trailing newline/whitespace
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
      line[--len] = '\0';
    }
    if (len == 0)
      continue;

    char key_to_parse[2048];
    const char *parse_ptr = line;

    // Handle different key formats
    if (strncmp(line, "AAAA", 4) == 0) {
      // Bare base64 key - add prefix
      snprintf(key_to_parse, sizeof(key_to_parse), "ssh-ed25519 %s", line);
      parse_ptr = key_to_parse;
      log_debug("Added ssh-ed25519 prefix to bare base64 key");
    } else if (strncmp(line, "ssh-ed25519 ", 12) == 0) {
      // Standard authorized_keys format
      parse_ptr = line;
    } else {
      // Might be known_hosts format (hostname ssh-ed25519 AAAA...)
      // Look for "ssh-ed25519" in the line
      char *ssh_prefix = strstr(line, "ssh-ed25519 ");
      if (ssh_prefix) {
        // Skip hostname and parse from ssh-ed25519 onwards
        parse_ptr = ssh_prefix;
        log_debug("Detected known_hosts format, parsing from ssh-ed25519 onwards");
      } else {
        // Unknown format - try parsing as-is
        parse_ptr = line;
      }
    }

    // Try to parse the key
    if (parse_public_key(parse_ptr, &keys[*num_keys]) == 0) {
      (*num_keys)++;
    } else {
      log_warn("Failed to parse key from file line: %s", line);
    }
  }

  if (pgp_block) {
    free(pgp_block);
  }

  fclose(f);
  return (*num_keys > 0) ? 0 : -1;
}

// Parse client keys from file or comma-separated list
// Supports:
// - File path (authorized_keys or known_hosts format)
// - Comma-separated keys: "ssh-ed25519 AAAA...,ssh-ed25519 BBBB..."
// - Single key: "ssh-ed25519 AAAA..." or "AAAA..."
int parse_client_keys(const char *input, public_key_t *keys, size_t *num_keys, size_t max_keys) {
  *num_keys = 0;

  if (!input || strlen(input) == 0) {
    return -1;
  }

  // Check if input contains comma - if so, it's comma-separated keys
  if (strchr(input, ',') != NULL) {
    log_debug("Parsing comma-separated keys: %s", input);

    // Make a copy since strtok modifies the string
    char *input_copy = strdup(input);
    if (!input_copy) {
      log_error("Failed to allocate memory for key parsing");
      return -1;
    }

    char *key_str = strtok(input_copy, ",");
    while (key_str && *num_keys < max_keys) {
      // Skip leading whitespace
      while (*key_str == ' ' || *key_str == '\t') {
        key_str++;
      }

      // Check if this is a github:/gitlab: reference that might have multiple keys
      if (strncmp(key_str, "github:", 7) == 0 || strncmp(key_str, "gitlab:", 7) == 0) {
        // This might return multiple keys - parse it and add all
        public_key_t temp_key;
        char **fetched_keys = NULL;
        size_t num_fetched = 0;

        // Parse to get all keys from this source
        if (parse_public_key(key_str, &temp_key) == 0) {
          // Successfully parsed - but this only gives us the first key
          // We need to re-fetch to get ALL keys
          bool is_github = (strncmp(key_str, "github:", 7) == 0);
          bool is_gpg = (strstr(key_str, ".gpg") != NULL);
          const char *username = key_str + 7;

          // Extract username (remove .ssh or .gpg suffix if present)
          char username_buf[256];
          const char *dot = strrchr(username, '.');
          if (dot && (strcmp(dot, ".ssh") == 0 || strcmp(dot, ".gpg") == 0)) {
            size_t len = dot - username;
            if (len < sizeof(username_buf)) {
              strncpy(username_buf, username, len);
              username_buf[len] = '\0';
              username = username_buf;
            }
          }

          // Fetch all keys
          if (is_github) {
            if (is_gpg) {
              fetch_github_gpg_keys(username, &fetched_keys, &num_fetched);
            } else {
              fetch_github_ssh_keys(username, &fetched_keys, &num_fetched);
            }
          } else {
            if (is_gpg) {
              fetch_gitlab_gpg_keys(username, &fetched_keys, &num_fetched);
            } else {
              fetch_gitlab_ssh_keys(username, &fetched_keys, &num_fetched);
            }
          }

          // Parse each fetched key
          if (fetched_keys && num_fetched > 0) {
            for (size_t i = 0; i < num_fetched && *num_keys < max_keys; i++) {
              if (is_gpg) {
                if (parse_gpg_key(fetched_keys[i], &keys[*num_keys]) == 0) {
                  (*num_keys)++;
                  log_info("Added GPG key %zu from %s", i + 1, key_str);
                }
              } else {
                if (parse_public_key(fetched_keys[i], &keys[*num_keys]) == 0) {
                  (*num_keys)++;
                  log_info("Added SSH key %zu from %s", i + 1, key_str);
                }
              }
              free(fetched_keys[i]);
            }
            free(fetched_keys);
          }
        } else {
          log_warn("Failed to parse key source: %s", key_str);
        }
      } else {
        // Check if this is a file path
        FILE *test_file = fopen(key_str, "r");
        if (test_file) {
          fclose(test_file);
          // It's a file - load keys from it
          size_t keys_before = *num_keys;
          if (parse_keys_from_file(key_str, keys, num_keys, max_keys) == 0) {
            size_t keys_added = *num_keys - keys_before;
            log_info("Loaded %zu key(s) from file: %s", keys_added, key_str);
          } else {
            log_warn("Failed to load keys from file: %s", key_str);
          }
        } else {
          // Not a file - try parsing as a raw key
          // Add "ssh-ed25519 " prefix if it's just the base64 part
          char key_with_prefix[2048];
          if (strncmp(key_str, "ssh-ed25519 ", 12) != 0 && strncmp(key_str, "AAAA", 4) == 0) {
            snprintf(key_with_prefix, sizeof(key_with_prefix), "ssh-ed25519 %s", key_str);
            key_str = key_with_prefix;
          }

          if (parse_public_key(key_str, &keys[*num_keys]) == 0) {
            (*num_keys)++;
            log_debug("Parsed key %zu from comma-separated list", *num_keys);
          } else {
            log_warn("Failed to parse key from comma-separated list: %s", key_str);
          }
        }
      }

      key_str = strtok(NULL, ",");
    }

    free(input_copy);
    return (*num_keys > 0) ? 0 : -1;
  }

  // Try as file path first
  FILE *f = fopen(input, "r");
  if (f) {
    fclose(f);
    log_debug("Parsing keys from file: %s", input);
    return parse_keys_from_file(input, keys, num_keys, max_keys);
  }

  // Not a file and no comma - try as single key or github:/gitlab: reference
  log_debug("Parsing as single key or remote reference: %s", input);

  // Check if this is a github:/gitlab: reference that might have multiple keys
  if (strncmp(input, "github:", 7) == 0 || strncmp(input, "gitlab:", 7) == 0) {
    bool is_github = (strncmp(input, "github:", 7) == 0);
    bool is_gpg = (strstr(input, ".gpg") != NULL);
    const char *username = input + 7;

    // Extract username (remove .ssh or .gpg suffix if present)
    char username_buf[256];
    const char *dot = strrchr(username, '.');
    if (dot && (strcmp(dot, ".ssh") == 0 || strcmp(dot, ".gpg") == 0)) {
      size_t len = dot - username;
      if (len < sizeof(username_buf)) {
        strncpy(username_buf, username, len);
        username_buf[len] = '\0';
        username = username_buf;
      }
    }

    // Fetch all keys
    char **fetched_keys = NULL;
    size_t num_fetched = 0;

    if (is_github) {
      if (is_gpg) {
        if (fetch_github_gpg_keys(username, &fetched_keys, &num_fetched) != 0) {
          log_error("Failed to fetch GPG keys for GitHub user: %s", username);
          return -1;
        }
      } else {
        if (fetch_github_ssh_keys(username, &fetched_keys, &num_fetched) != 0) {
          log_error("Failed to fetch SSH keys for GitHub user: %s", username);
          return -1;
        }
      }
    } else {
      if (is_gpg) {
        if (fetch_gitlab_gpg_keys(username, &fetched_keys, &num_fetched) != 0) {
          log_error("Failed to fetch GPG keys for GitLab user: %s", username);
          return -1;
        }
      } else {
        if (fetch_gitlab_ssh_keys(username, &fetched_keys, &num_fetched) != 0) {
          log_error("Failed to fetch SSH keys for GitLab user: %s", username);
          return -1;
        }
      }
    }

    // Parse each fetched key
    if (fetched_keys && num_fetched > 0) {
      for (size_t i = 0; i < num_fetched && *num_keys < max_keys; i++) {
        if (is_gpg) {
          if (parse_gpg_key(fetched_keys[i], &keys[*num_keys]) == 0) {
            (*num_keys)++;
            log_info("Added GPG key %zu from %s", i + 1, input);
          }
        } else {
          if (parse_public_key(fetched_keys[i], &keys[*num_keys]) == 0) {
            (*num_keys)++;
            log_info("Added SSH key %zu from %s", i + 1, input);
          }
        }
        free(fetched_keys[i]);
      }
      free(fetched_keys);
      return (*num_keys > 0) ? 0 : -1;
    }

    log_error("No keys found for: %s", input);
    return -1;
  }

  // Regular single key - not a github:/gitlab: reference
  // Add "ssh-ed25519 " prefix if it's just the base64 part
  char key_with_prefix[2048];
  const char *key_to_parse = input;
  if (strncmp(input, "ssh-ed25519 ", 12) != 0 && strncmp(input, "AAAA", 4) == 0) {
    snprintf(key_with_prefix, sizeof(key_with_prefix), "ssh-ed25519 %s", input);
    key_to_parse = key_with_prefix;
  }

  if (parse_public_key(key_to_parse, &keys[0]) == 0) {
    *num_keys = 1;
    return 0;
  }

  log_error("Failed to parse client keys: %s", input);
  return -1;
}

// Convert public key to display format (ssh-ed25519 or x25519 hex)
void format_public_key(const public_key_t *key, char *output, size_t output_size) {
  if (!key || !output) {
    if (output && output_size > 0) {
      output[0] = '\0';
    }
    return;
  }

  switch (key->type) {
  case KEY_TYPE_ED25519:
    // TODO: Convert back to SSH format
    if (strlen(key->comment) > 0) {
      snprintf(output, output_size, "ssh-ed25519 (converted to X25519) %s", key->comment);
    } else {
      snprintf(output, output_size, "ssh-ed25519 (converted to X25519)");
    }
    break;
  case KEY_TYPE_X25519:
    // Show as hex
    char hex[65];
    for (int i = 0; i < 32; i++) {
      snprintf(hex + i * 2, 3, "%02x", key->key[i]);
    }
    if (strlen(key->comment) > 0) {
      snprintf(output, output_size, "x25519 %s %s", hex, key->comment);
    } else {
      snprintf(output, output_size, "x25519 %s", hex);
    }
    break;
  case KEY_TYPE_GPG:
    if (strlen(key->comment) > 0) {
      snprintf(output, output_size, "gpg (derived to X25519) %s", key->comment);
    } else {
      snprintf(output, output_size, "gpg (derived to X25519)");
    }
    break;
  default:
    snprintf(output, output_size, "unknown key type");
    break;
  }
}

// Sign a message with Ed25519 (uses SSH agent if available, otherwise in-memory key)
// This is the main signing function that abstracts SSH agent vs in-memory signing
// Returns: 0 on success, -1 on failure
int ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len, uint8_t signature[64]) {
  if (!key || !message || !signature) {
    return -1;
  }

  if (key->use_gpg_agent) {
    // Sign via GPG agent protocol
    fprintf(stderr, "ed25519_sign_message: Using GPG agent to sign message (%zu bytes)\n", message_len);

    int agent_fd = gpg_agent_connect();
    if (agent_fd < 0) {
      fprintf(stderr, "ed25519_sign_message: Failed to connect to GPG agent\n");
      return -1;
    }

    size_t sig_len = 0;
    int result = gpg_agent_sign(agent_fd, key->gpg_keygrip, message, message_len, signature, &sig_len);

    gpg_agent_disconnect(agent_fd);

    if (result != 0 || sig_len != 64) {
      fprintf(stderr, "ed25519_sign_message: GPG agent signing failed\n");
      return -1;
    }

    fprintf(stderr, "ed25519_sign_message: Successfully signed with GPG agent\n");
    return 0;
  }

  if (key->use_ssh_agent) {
    // Sign via SSH agent protocol
    fprintf(stderr, "ed25519_sign_message: Using SSH agent to sign message (%zu bytes)\n", message_len);

    const char *ssh_auth_sock = getenv("SSH_AUTH_SOCK");
    if (!ssh_auth_sock) {
      fprintf(stderr, "ed25519_sign_message: SSH_AUTH_SOCK not set\n");
      return -1;
    }

    // Connect to SSH agent Unix socket
    int agent_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (agent_fd < 0) {
      fprintf(stderr, "ed25519_sign_message: Failed to create socket\n");
      return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    SAFE_STRNCPY(addr.sun_path, ssh_auth_sock, sizeof(addr.sun_path) - 1);

    if (connect(agent_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      fprintf(stderr, "ed25519_sign_message: Failed to connect to SSH agent: %s\n", strerror(errno));
      close(agent_fd);
      return -1;
    }

    // Build SSH public key blob for the identity
    // Format: [type_len:uint32]["ssh-ed25519"][key_len:uint32][32 bytes key]
    uint8_t pubkey_blob[51];
    size_t blob_offset = 0;

    // Type string length (11 = strlen("ssh-ed25519"))
    pubkey_blob[blob_offset++] = 0;
    pubkey_blob[blob_offset++] = 0;
    pubkey_blob[blob_offset++] = 0;
    pubkey_blob[blob_offset++] = 11;
    memcpy(pubkey_blob + blob_offset, "ssh-ed25519", 11);
    blob_offset += 11;

    // Key length (32)
    pubkey_blob[blob_offset++] = 0;
    pubkey_blob[blob_offset++] = 0;
    pubkey_blob[blob_offset++] = 0;
    pubkey_blob[blob_offset++] = 32;
    memcpy(pubkey_blob + blob_offset, key->public_key, 32);
    blob_offset += 32;

    // Build SSH agent sign request
    // Message format: [length:uint32][type:byte][pubkey:string][data:string][flags:uint32]
    uint32_t request_len = 1 + 4 + 51 + 4 + message_len + 4; // type + pubkey_string + data_string + flags
    uint8_t *request;
    SAFE_MALLOC(request, 4 + request_len, uint8_t *);

    size_t offset = 0;
    // Total length (network byte order)
    request[offset++] = (request_len >> 24) & 0xFF;
    request[offset++] = (request_len >> 16) & 0xFF;
    request[offset++] = (request_len >> 8) & 0xFF;
    request[offset++] = request_len & 0xFF;

    // Message type: SSH_AGENTC_SIGN_REQUEST (13)
    request[offset++] = 13;

    // Public key blob (as string: length + data)
    request[offset++] = 0;
    request[offset++] = 0;
    request[offset++] = 0;
    request[offset++] = 51;
    memcpy(request + offset, pubkey_blob, 51);
    offset += 51;

    // Data to sign (as string: length + data)
    request[offset++] = (message_len >> 24) & 0xFF;
    request[offset++] = (message_len >> 16) & 0xFF;
    request[offset++] = (message_len >> 8) & 0xFF;
    request[offset++] = message_len & 0xFF;
    memcpy(request + offset, message, message_len);
    offset += message_len;

    // Flags (0 = standard signing)
    request[offset++] = 0;
    request[offset++] = 0;
    request[offset++] = 0;
    request[offset++] = 0;

    // Send request to agent
    ssize_t written = write(agent_fd, request, 4 + request_len);
    free(request);
    if (written != (ssize_t)(4 + request_len)) {
      fprintf(stderr, "ed25519_sign_message: Failed to write to SSH agent\n");
      close(agent_fd);
      return -1;
    }

    // Read response length
    uint8_t response_len_buf[4];
    ssize_t read_len = read(agent_fd, response_len_buf, 4);
    if (read_len != 4) {
      fprintf(stderr, "ed25519_sign_message: Failed to read response length from SSH agent\n");
      close(agent_fd);
      return -1;
    }

    uint32_t response_len =
        (response_len_buf[0] << 24) | (response_len_buf[1] << 16) | (response_len_buf[2] << 8) | response_len_buf[3];

    if (response_len > 8192) { // Sanity check
      fprintf(stderr, "ed25519_sign_message: Response too large: %u\n", response_len);
      close(agent_fd);
      return -1;
    }

    // Read response
    uint8_t *response;
    SAFE_MALLOC(response, response_len, uint8_t *);
    read_len = read(agent_fd, response, response_len);
    close(agent_fd);

    if (read_len != (ssize_t)response_len) {
      fprintf(stderr, "ed25519_sign_message: Failed to read full response from SSH agent\n");
      free(response);
      return -1;
    }

    // Parse response
    // Format: [type:byte][signature_blob:string]
    if (response[0] != 14) { // SSH_AGENT_SIGN_RESPONSE
      fprintf(stderr, "ed25519_sign_message: Unexpected response type: %d\n", response[0]);
      free(response);
      return -1;
    }

    // Read signature blob (string: length + data)
    if (response_len < 5) {
      fprintf(stderr, "ed25519_sign_message: Response too short\n");
      free(response);
      return -1;
    }

    uint32_t sig_blob_len = (response[1] << 24) | (response[2] << 16) | (response[3] << 8) | response[4];

    // SSH signature blob format: [type_len:uint32]["ssh-ed25519"][signature_len:uint32][64 bytes signature]
    // Minimum size: 4 (type_len) + 11 ("ssh-ed25519") + 4 (sig_len) + 64 (signature) = 83 bytes
    if (sig_blob_len < 4 + 11 + 4 + 64) {
      fprintf(stderr, "ed25519_sign_message: Signature blob too short: %u (expected >= 83)\n", sig_blob_len);
      free(response);
      return -1;
    }

    // Skip to signature data: 1 (type) + 4 (blob_len) + 4 (type_len) + 11 ("ssh-ed25519") + 4 (sig_len)
    size_t sig_offset = 1 + 4 + 4 + 11 + 4;
    if (sig_offset + 64 > response_len) {
      fprintf(stderr, "ed25519_sign_message: Not enough data for signature\n");
      free(response);
      return -1;
    }

    memcpy(signature, response + sig_offset, 64);
    free(response);

    fprintf(stderr, "ed25519_sign_message: Successfully signed with SSH agent\n");
    return 0;
  } else {
    // Use in-memory Ed25519 key to sign
    if (key->type != KEY_TYPE_ED25519) {
      fprintf(stderr, "ed25519_sign_message: Key type is not Ed25519\n");
      return -1;
    }

    // libsodium's crypto_sign_detached expects:
    // - signature: output buffer (64 bytes)
    // - message: message to sign
    // - message_len: length of message
    // - secret_key: 64-byte Ed25519 secret key (seed + public key)
    if (crypto_sign_detached(signature, NULL, message, message_len, key->key.ed25519) != 0) {
      fprintf(stderr, "ed25519_sign_message: crypto_sign_detached failed\n");
      return -1;
    }

    return 0;
  }
}

// Verify an Ed25519 signature
// Returns: 0 on success (valid signature), -1 on failure
int ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                             const uint8_t signature[64]) {
  if (!public_key || !message || !signature) {
    return -1;
  }

  // IMPORTANT: This verification must match the signing method!
  //
  // For GPG agent Ed25519 keys:
  // - GPG agent signs using libgcrypt's EdDSA with SHA-512 pre-hashing
  // - We must verify with libgcrypt's gcry_pk_verify()
  //
  // For standard Ed25519 (SSH agent):
  // - SSH agent signs: EdDSA_sign(message)  [raw message, Ed25519 does internal hashing]
  // - We must verify:  EdDSA_verify(message, signature, public_key)
  //
  // Since we don't know which type of key signed this, we try all approaches:

  // Try 1: Standard Ed25519 verification (raw message) - for SSH keys
  log_debug("ed25519_verify_signature: Trying standard Ed25519 verification (raw message)");
  if (crypto_sign_verify_detached(signature, message, message_len, public_key) == 0) {
    log_info("ed25519_verify_signature: SUCCESS with standard Ed25519 verification");
    return 0; // Success with standard Ed25519 (SSH keys)
  }
  log_debug("ed25519_verify_signature: Standard verification failed, trying GPG libgcrypt");

  // Try 2: GPG libgcrypt verification - for GPG keys (when libgcrypt is available)
#ifdef HAVE_LIBGCRYPT
  log_debug("ed25519_verify_signature: Trying GPG libgcrypt verification");
  if (gpg_verify_signature(public_key, message, message_len, signature) == 0) {
    log_info("ed25519_verify_signature: SUCCESS with GPG libgcrypt verification");
    return 0; // Success with GPG keys
  }
  log_debug("ed25519_verify_signature: GPG libgcrypt verification failed");
#else
  log_debug("ed25519_verify_signature: libgcrypt not available, skipping GPG verification");
#endif

  log_error("ed25519_verify_signature: FAILED - all verification methods failed");
  return -1;
}

// =============================================================================
// Crypto Handshake Integration (shared between client and server)
// =============================================================================

/**
 * Configure SSH key for handshake context (shared between client and server)
 *
 * This function eliminates code duplication between client and server crypto initialization.
 * It handles both SSH agent mode and in-memory mode transparently.
 *
 * @param ctx Crypto handshake context to configure
 * @param private_key SSH private key to use
 * @return 0 on success, -1 on failure
 */
int crypto_setup_ssh_key_for_handshake(struct crypto_handshake_context_t *ctx_param, const private_key_t *private_key) {
  if (!ctx_param || !private_key) {
    return -1;
  }

  // Cast to typedef for member access (keys.h only has forward declaration)
  (void)ctx_param; // Unused - handshake context uses ephemeral keys regardless of SSH mode

  // SECURITY ARCHITECTURE:
  // We ALWAYS use ephemeral X25519 keys for encryption (forward secrecy).
  // The SSH key is ONLY used for authentication (Ed25519 signatures).
  //
  // This provides:
  // 1. Forward secrecy: Past sessions remain secure even if SSH key is compromised later
  // 2. Consistency: Same security model regardless of SSH agent availability
  // 3. SSH-style design: Matches SSH protocol (ephemeral DH + long-term identity)
  //
  // The handshake protocol binds the ephemeral encryption key to the identity key via signature:
  //   Server sends: [ephemeral_X25519:32][identity_Ed25519:32][signature:64]
  //   where: signature = sign(identity_private_key, ephemeral_X25519)
  //
  // This proves: "I possess identity_Ed25519 private key AND I'm using ephemeral_X25519 for encryption"

  if (private_key->use_ssh_agent) {
    // SSH agent mode: Use agent for signing, ephemeral keys for encryption
    log_info("SSH agent mode: ephemeral encryption + agent authentication");
  } else {
    // In-memory mode: Use in-memory key for signing, ephemeral keys for encryption
    log_info("In-memory mode: ephemeral encryption + in-memory authentication");
  }

  // Keep the ephemeral X25519 keys generated by crypto_handshake_init()
  // Both modes now have identical forward secrecy guarantees
  return 0;
}

/**
 * Validate SSH key file before parsing (shared between client and server)
 */
int validate_ssh_key_file(const char *key_path) {
  if (!key_path) {
    return -1;
  }

  // Verify file exists and is accessible
  struct stat st;
  if (stat(key_path, &st) != 0) {
    // File doesn't exist
    log_error("Key file not found: %s", key_path);
    log_error("Please check the file path or use --password for password-based encryption");
    return -1;
  }

  // Check if file is readable
  FILE *test_file = fopen(key_path, "r");
  if (test_file == NULL) {
    // File exists but can't be read (permission denied)
    log_error("Cannot read key file: %s", key_path);
    log_error("Please check file permissions (should be 600 or 400)");
    return -1;
  }

  // Check if this is an SSH key file by looking for the header
  char header[256];
  bool is_ssh_key_file = false;
  if (fgets(header, sizeof(header), test_file) != NULL) {
    if (strstr(header, "BEGIN OPENSSH PRIVATE KEY") != NULL || strstr(header, "BEGIN RSA PRIVATE KEY") != NULL ||
        strstr(header, "BEGIN EC PRIVATE KEY") != NULL) {
      is_ssh_key_file = true;
    }
  }
  fclose(test_file);

  if (!is_ssh_key_file) {
    log_error("File is not a valid SSH key: %s", key_path);
    log_error("Expected SSH private key format (BEGIN OPENSSH PRIVATE KEY)");
    log_error("Use --password for password-based encryption instead");
    return -1;
  }

  // Check permissions for SSH key files (should be 600 or 400)
  if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
    log_warn("SSH key file %s has overly permissive permissions: %o", key_path, st.st_mode & 0777);
    log_warn("Recommended: chmod 600 %s", key_path);
    log_warn("Continuing anyway, but this is a security risk");
  }

  return 0;
}
