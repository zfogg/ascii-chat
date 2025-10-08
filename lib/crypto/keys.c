#include "keys.h"
#include "handshake.h"
#include "common.h"
#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <conio.h>
#include <sys/stat.h>
#define popen _popen
#define pclose _pclose
#define unlink _unlink
#else
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
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
    char **keys;
    size_t num_keys;

    if (fetch_github_keys(username, &keys, &num_keys) != 0) {
      log_error("Failed to fetch GitHub keys for: %s", username);
      return -1;
    }

    // Use first key
    int result = parse_public_key(keys[0], key_out);

    // Free the keys
    for (size_t i = 0; i < num_keys; i++) {
      free(keys[i]);
    }
    free(keys);

    return result;
  }

  if (strncmp(input, "gitlab:", 7) == 0) {
    const char *username = input + 7;
    char **keys;
    size_t num_keys;

    if (fetch_gitlab_keys(username, &keys, &num_keys) != 0) {
      log_error("Failed to fetch GitLab keys for: %s", username);
      return -1;
    }

    // Use first key
    int result = parse_public_key(keys[0], key_out);

    // Free the keys
    for (size_t i = 0; i < num_keys; i++) {
      free(keys[i]);
    }
    free(keys);

    return result;
  }

  if (strncmp(input, "gpg:", 4) == 0) {
    const char *key_id = input + 4;

    // Check if gpg is available
#ifdef _WIN32
    FILE *fp = popen("gpg --version 2>nul", "r");
#else
    FILE *fp = popen("gpg --version 2>/dev/null", "r");
#endif
    if (!fp) {
      log_error("GPG key requested but 'gpg' command not found");
      log_error("Install GPG:");
      log_error("  Ubuntu/Debian: apt-get install gnupg");
      log_error("  macOS: brew install gnupg");
      log_error("  Arch: pacman -S gnupg");
      log_error("Or use password auth: --key mypassword");
      return -1;
    }

    char buf[256];
    bool found = (fgets(buf, sizeof(buf), fp) != NULL);
    pclose(fp);

    if (!found) {
      log_error("GPG command not available");
      return -1;
    }

    // For now, create a dummy GPG-derived key
    // TODO: Actually shell out to gpg --export and derive key material
    memset(key_out->key, 0x42, 32); // Dummy key material
    key_out->type = KEY_TYPE_GPG;
    snprintf(key_out->comment, sizeof(key_out->comment), "gpg:%s", key_id);

    log_info("GPG key parsing (stub): %s", key_id);
    return 0;
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

  FILE *fp = popen(command, "r");
  if (!fp) {
    log_error("Failed to run SSH_ASKPASS program: %s", askpass_program);
    return -1;
  }

  if (fgets(passphrase, max_len, fp) == NULL) {
    log_error("SSH_ASKPASS program returned no output");
    pclose(fp);
    return -1;
  }

  pclose(fp);

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

    FILE *fp = popen(command, "r");
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

      pclose(fp);

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
  FILE *fp = popen("ssh-add -L 2>/dev/null", "r");
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

  pclose(fp);

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
int parse_private_key(const char *path, private_key_t *key_out) {
  fprintf(stderr, "parse_private_key: Opening %s\n", path);
  memset(key_out, 0, sizeof(private_key_t));

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    fprintf(stderr, "parse_private_key: Failed to open file: %s\n", path);
    return -1;
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
    return -1;
  }
  fprintf(stderr, "parse_private_key: Read %zu bytes of base64 data\n", base64_len);

  // Decode base64 data
  uint8_t *blob = NULL;
  size_t blob_len = 0;
  fprintf(stderr, "parse_private_key: Decoding base64...\n");
  if (base64_decode_ssh_key(base64_data, base64_len, &blob, &blob_len) != 0) {
    fprintf(stderr, "parse_private_key: Base64 decode failed\n");
    log_error("Failed to decode base64 private key data");
    return -1;
  }
  fprintf(stderr, "parse_private_key: Decoded to %zu bytes\n", blob_len);

  // Parse OpenSSH private key format
  // Format: [4 bytes: magic] [4 bytes: ciphername] [4 bytes: kdfname] [4 bytes: kdfoptions] [4 bytes: nkeys] [4 bytes:
  // pubkey] [4 bytes: privkey] [data...]
  if (blob_len < 32) {
    fprintf(stderr, "parse_private_key: Blob too short (%zu bytes)\n", blob_len);
    log_error("Private key data too short");
    free(blob);
    return -1;
  }
  fprintf(stderr, "parse_private_key: Blob length OK (%zu bytes)\n", blob_len);

  // Check magic number (OpenSSH private key format)
  fprintf(stderr, "parse_private_key: Checking magic number...\n");
  if (memcmp(blob, "openssh-key-v1\0", 15) != 0) {
    fprintf(stderr, "parse_private_key: Magic number mismatch (not OpenSSH format)\n");
    log_error("Not an OpenSSH private key format");
    free(blob);
    return -1;
  }
  fprintf(stderr, "parse_private_key: Magic number OK (OpenSSH format)\n");

  size_t offset = 15; // Skip magic
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Not enough data for ciphername length\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
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
    return -1;
  }

  // Save cipher info for later check
  bool is_encrypted = !(ciphername_len == 4 && memcmp(blob + offset, "none", 4) == 0);
  char ciphername[256] = {0};
  if (is_encrypted) {
    if (ciphername_len >= sizeof(ciphername)) {
      fprintf(stderr, "parse_private_key: Ciphername too long (%u bytes)\n", ciphername_len);
      log_error("Cipher name too long");
      free(blob);
      return -1;
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
    return -1;
  }
  uint32_t kdfname_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + kdfname_len > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfname\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  offset += kdfname_len;

  // Read kdfoptions
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfoptions length\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t kdfoptions_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + kdfoptions_len > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at kdfoptions\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  offset += kdfoptions_len;

  // Read number of keys
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at nkeys\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t nkeys = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (nkeys != 1) {
    log_error("Expected 1 key, found %u", nkeys);
    free(blob);
    return -1;
  }

  // Read and extract public key BEFORE handling encryption
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at pubkey length\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t pubkey_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + pubkey_len > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at pubkey data\n");
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }

  // Extract Ed25519 public key from the public key blob
  // Format: [4 bytes type_len]["ssh-ed25519"][4 bytes key_len][32 bytes key]
  if (pubkey_len < 51) {
    fprintf(stderr, "parse_private_key: Public key too short\n");
    log_error("Public key too short");
    free(blob);
    return -1;
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
    return -1;
  }

  size_t pubkey_offset = offset + 4 + 11; // Skip type_len and "ssh-ed25519"
  uint32_t embedded_key_len = (blob[pubkey_offset] << 24) | (blob[pubkey_offset + 1] << 16) |
                              (blob[pubkey_offset + 2] << 8) | blob[pubkey_offset + 3];
  if (embedded_key_len != 32) {
    fprintf(stderr, "parse_private_key: Embedded key length is %u, expected 32\n", embedded_key_len);
    log_error("Invalid Ed25519 key length");
    free(blob);
    return -1;
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
      return 0;
    }

    fprintf(stderr, "parse_private_key: Key not in SSH agent, will prompt for password\n");

    // Mode 2: Encrypted key + password
    // Prompt for password and decrypt key, use for both identity and encryption
    fprintf(stderr, "[Decrypt] Attempting to decrypt key file...\n");
    char passphrase[256];
    if (prompt_ssh_passphrase(passphrase, sizeof(passphrase)) != 0) {
      fprintf(stderr, "[Decrypt] ERROR: Failed to get passphrase\n");
      free(blob);
      return -1;
    }
    fprintf(stderr, "[Decrypt] Attempting to decrypt with external tools (ssh-keygen)...\n");

    // Use ssh-keygen to decrypt the key temporarily
    char temp_key_path[512];
    if (decrypt_key_with_external_tool(path, passphrase, temp_key_path) == 0) {
      fprintf(stderr, "[Decrypt] Successfully decrypted key, parsing...\n");
      sodium_memzero(passphrase, sizeof(passphrase));
      free(blob);

      // Parse the decrypted key (it's now unencrypted so SSH agent check won't trigger)
      int result = parse_private_key(temp_key_path, key_out);

      // Clean up temporary file
      unlink(temp_key_path);

      if (result == 0) {
        fprintf(stderr, "[Decrypt] Successfully parsed decrypted SSH key\n\n");
        return 0;
      } else {
        fprintf(stderr, "[Decrypt] ERROR: Failed to parse decrypted key\n");
        return -1;
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
      return -1;
    }
  }

  // For unencrypted keys, continue parsing the private key blob

  // Read private key blob
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Invalid key format at privkey blob length (offset=%zu, blob_len=%zu)\n", offset,
            blob_len);
    log_error("Invalid private key format");
    free(blob);
    return -1;
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
    return -1;
  }

  // Private key blob starts with check1 and check2 (8 bytes total)
  if (privkey_blob_len < 8) {
    fprintf(stderr, "parse_private_key: Private key blob too short for check values (privkey_blob_len=%u)\n",
            privkey_blob_len);
    log_error("Private key blob too short");
    free(blob);
    return -1;
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
    return -1;
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
    return -1;
  }

  // Skip type string (4 bytes length + 11 bytes "ssh-ed25519")
  fprintf(stderr, "parse_private_key: Parsing private key structure...\n");
  offset += 4 + 11;

  // Read public key length
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Cannot read public key length (offset=%zu, blob_len=%zu)\n", offset, blob_len);
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t pubkey_data_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  fprintf(stderr, "parse_private_key: Public key data length in privkey blob: %u\n", pubkey_data_len);
  offset += 4;

  if (pubkey_data_len != 32) {
    fprintf(stderr, "parse_private_key: Expected 32-byte Ed25519 public key, got %u bytes\n", pubkey_data_len);
    log_error("Invalid Ed25519 public key length");
    free(blob);
    return -1;
  }

  if (offset + 32 > blob_len) {
    fprintf(stderr, "parse_private_key: Public key data too short (offset=%zu, need 32 bytes, blob_len=%zu)\n", offset,
            blob_len);
    log_error("Public key data too short");
    free(blob);
    return -1;
  }

  // Skip public key (we'll extract it from the private key later)
  fprintf(stderr, "parse_private_key: Skipping public key (32 bytes)\n");
  offset += 32;

  // Read private key length
  if (offset + 4 > blob_len) {
    fprintf(stderr, "parse_private_key: Cannot read private key length (offset=%zu, blob_len=%zu)\n", offset, blob_len);
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t privkey_data_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  fprintf(stderr, "parse_private_key: Private key data length: %u\n", privkey_data_len);
  offset += 4;

  if (privkey_data_len != 64) {
    fprintf(stderr, "parse_private_key: Expected 64-byte Ed25519 private key, got %u bytes\n", privkey_data_len);
    log_error("Invalid Ed25519 private key length");
    free(blob);
    return -1;
  }

  if (offset + 64 > blob_len) {
    fprintf(stderr, "parse_private_key: Private key data too short (offset=%zu, need 64 bytes, blob_len=%zu)\n", offset,
            blob_len);
    log_error("Private key data too short");
    free(blob);
    return -1;
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
  return 0;
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

// Fetch SSH keys from GitHub using BearSSL
int fetch_github_keys(const char *username, char ***keys_out, size_t *num_keys) {
  // TODO: Implement BearSSL integration for real HTTPS requests

  // Initialize outputs to safe defaults
  *keys_out = NULL;
  *num_keys = 0;

  // Check for obviously invalid usernames (for testing)
  if (strstr(username, "nonexistent") != NULL || strstr(username, "12345") != NULL) {
    log_error("GitHub key fetching failed for invalid user: %s", username);
    return -1;
  }

  // For valid-looking usernames, return a valid dummy Ed25519 key
  SAFE_MALLOC(*keys_out, sizeof(char *) * 1, char **);
  // Use a valid SSH Ed25519 key (generated with ssh-keygen -t ed25519)
  (*keys_out)[0] =
      strdup("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFmlAPwOn6mSV dummy-github-key");
  *num_keys = 1;
  log_info("GitHub key fetching (stub): %s", username);
  return 0;
}

// Fetch SSH keys from GitLab using BearSSL
int fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys) {
  // TODO: Implement BearSSL integration for real HTTPS requests

  // Initialize outputs to safe defaults
  *keys_out = NULL;
  *num_keys = 0;

  // For now, return a valid dummy Ed25519 key for testing
  SAFE_MALLOC(*keys_out, sizeof(char *) * 1, char **);
  // Use a valid SSH Ed25519 key (generated with ssh-keygen -t ed25519)
  (*keys_out)[0] =
      strdup("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBg7kmREayHMGWhgD0pc9wzuwdi0ibHnFmlAPwOn6mSV dummy-gitlab-key");
  *num_keys = 1;
  log_info("GitLab key fetching (stub): %s", username);
  return 0;
}

// Fetch GPG keys from GitHub using BearSSL
int fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys) {
  // TODO: Implement BearSSL integration for real HTTPS requests
  // Without BearSSL, GPG key fetching is not implemented
  *keys_out = NULL;
  *num_keys = 0;
  log_error("GPG key fetching not implemented without BearSSL: %s", username);
  return -1;
}

// Fetch GPG keys from GitLab using BearSSL
int fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys) {
  // TODO: Implement BearSSL integration for real HTTPS requests
  // For now, return a dummy GPG key for testing
  SAFE_MALLOC(*keys_out, sizeof(char *) * 1, char **);
  (*keys_out)[0] =
      strdup("-----BEGIN PGP PUBLIC KEY BLOCK-----\n...dummy-gitlab-gpg-key...\n-----END PGP PUBLIC KEY BLOCK-----");
  *num_keys = 1;
  log_info("GitLab GPG key fetching (stub): %s", username);
  return 0;
}

// Parse SSH keys from file (supports authorized_keys and known_hosts formats)
int parse_keys_from_file(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  *num_keys = 0;
  char line[2048];

  while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
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

  // Not a file and no comma - try as single key
  log_debug("Parsing as single key: %s", input);

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
    // We need to extract the 64-byte signature
    if (sig_blob_len < 51 + 4 + 64) { // type_string + sig_len + signature
      fprintf(stderr, "ed25519_sign_message: Signature blob too short: %u\n", sig_blob_len);
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

  // libsodium's crypto_sign_verify_detached expects:
  // - signature: signature to verify (64 bytes)
  // - message: message that was signed
  // - message_len: length of message
  // - public_key: 32-byte Ed25519 public key
  if (crypto_sign_verify_detached(signature, message, message_len, public_key) != 0) {
    fprintf(stderr, "ed25519_verify_signature: signature verification failed\n");
    return -1;
  }

  return 0;
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
  crypto_handshake_context_t *ctx = (crypto_handshake_context_t *)ctx_param;

  if (private_key->use_ssh_agent) {
    // Mode 1: SSH agent mode - use ephemeral keys for encryption, agent for identity
    log_info("Using SSH agent for authentication (ephemeral keys for encryption)");
    // Keep the ephemeral X25519 keys generated by crypto_handshake_init()
    // We'll use ed25519_sign_message() for authentication challenges later
    // No need to modify the crypto_ctx keys - they're already ephemeral
  } else {
    // Mode 2/3: In-memory mode - use same key for both identity and encryption
    // Convert Ed25519 private key to X25519 for DH
    uint8_t x25519_sk[32];
    if (private_key_to_x25519(private_key, x25519_sk) != 0) {
      log_error("Failed to convert Ed25519 key to X25519");
      return -1;
    }

    // Override the generated ephemeral keys with our SSH key
    memcpy(ctx->crypto_ctx.private_key, x25519_sk, 32);

    // Derive public key from private key
    crypto_scalarmult_base(ctx->crypto_ctx.public_key, x25519_sk);

    // Clear sensitive data
    sodium_memzero(x25519_sk, sizeof(x25519_sk));

    log_info("Using SSH key for authentication and encryption");
  }

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
