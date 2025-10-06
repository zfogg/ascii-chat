#include "keys.h"
#include "common.h"
#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <conio.h>
#define popen _popen
#define pclose _pclose
#define unlink _unlink
#else
#include <unistd.h>
#include <termios.h>
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

// Convert Ed25519 public key to X25519 for DH
static int ed25519_to_x25519_pk(const uint8_t ed25519_pk[32], uint8_t x25519_pk[32]) {
  return crypto_sign_ed25519_pk_to_curve25519(x25519_pk, ed25519_pk);
}

// Convert Ed25519 private key to X25519 for DH
static int ed25519_to_x25519_sk(const uint8_t ed25519_sk[64], uint8_t x25519_sk[32]) {
  return crypto_sign_ed25519_sk_to_curve25519(x25519_sk, ed25519_sk);
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
    log_info("Using SSH_ASKPASS for passphrase input");
    return prompt_with_askpass(ssh_askpass, "SSH key passphrase:", passphrase, max_len);
  }

  // Try DISPLAY for GUI environments (like pinentry)
  const char *display = getenv("DISPLAY");
  if (display && strlen(display) > 0) {
    log_info("GUI environment detected, trying pinentry");
    return prompt_with_pinentry(passphrase, max_len);
  }

  // Fallback to terminal input (less secure)
  log_warn("No secure passphrase input available, using terminal input");
  log_info("SSH key is encrypted. Please enter the passphrase:");

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
        log_error("Failed to read passphrase");
        return -1;
      }
      tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    } else {
      // Fallback to normal input
      if (fgets(passphrase, max_len, stdin) == NULL) {
        log_error("Failed to read passphrase");
        return -1;
      }
    }
  } else {
    // Fallback to normal input
    if (fgets(passphrase, max_len, stdin) == NULL) {
      log_error("Failed to read passphrase");
      return -1;
    }
  }

  size_t len = strlen(passphrase);
  if (len > 0 && passphrase[len - 1] == '\n') {
    passphrase[len - 1] = '\0';
  }
#endif

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

// Check if SSH agent has Ed25519 keys and get the first one
static int get_ssh_agent_ed25519_key(char *public_key_out, size_t public_key_size) {
  // Check if SSH agent is running
  const char *ssh_auth_sock = getenv("SSH_AUTH_SOCK");
  if (!ssh_auth_sock) {
    log_debug("SSH agent not running (SSH_AUTH_SOCK not set)");
    return -1;
  }

  log_info("SSH agent detected, looking for Ed25519 keys...");

  // Try to get the public key using ssh-add -L
  // This will list all keys in the agent
  FILE *fp = popen("ssh-add -L", "r");
  if (!fp) {
    log_error("Failed to run ssh-add -L");
    return -1;
  }

  char line[1024];
  bool found_ed25519 = false;

  while (fgets(line, sizeof(line), fp)) {
    // Look for the first Ed25519 key
    if (strstr(line, "ssh-ed25519")) {
      log_info("Found Ed25519 key in SSH agent: %.50s...", line);

      // Copy the public key line
      size_t line_len = strlen(line);
      if (line_len < public_key_size) {
        strncpy(public_key_out, line, line_len - 1); // Remove newline
        public_key_out[line_len - 1] = '\0';
        found_ed25519 = true;
        break;
      } else {
        log_error("Public key too long for buffer");
        break;
      }
    }
  }

  pclose(fp);

  if (!found_ed25519) {
    log_debug("No Ed25519 keys found in SSH agent");
    return -1;
  }

  log_info("Using first Ed25519 key from SSH agent");
  return 0;
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

  // Use ssh-keygen to convert encrypted key to unencrypted
  char command[1024];
  snprintf(command, sizeof(command), "ssh-keygen -p -N \"\" -f \"%s\" -P \"%s\" -o \"%s\"", key_path, passphrase,
           temp_dir);

  log_info("Attempting to decrypt key with ssh-keygen...");
  log_debug("Command: %s", command);

  int result = system(command);
  if (result != 0) {
    log_error("Failed to decrypt key with ssh-keygen (exit code: %d)", result);
    return -1;
  }

  // Copy the temp path to output
  strncpy(temp_key_path, temp_dir, 511);
  temp_key_path[511] = '\0';

  log_info("Successfully decrypted key to temporary file: %s", temp_key_path);
  return 0;
}

// Parse SSH private key from file
int parse_private_key(const char *path, private_key_t *key_out) {
  memset(key_out, 0, sizeof(private_key_t));

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    log_error("Failed to open private key file: %s", path);
    return -1;
  }

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
    log_error("No private key data found in file: %s", path);
    return -1;
  }

  // Decode base64 data
  uint8_t *blob = NULL;
  size_t blob_len = 0;
  if (base64_decode_ssh_key(base64_data, base64_len, &blob, &blob_len) != 0) {
    log_error("Failed to decode base64 private key data");
    return -1;
  }

  // Parse OpenSSH private key format
  // Format: [4 bytes: magic] [4 bytes: ciphername] [4 bytes: kdfname] [4 bytes: kdfoptions] [4 bytes: nkeys] [4 bytes:
  // pubkey] [4 bytes: privkey] [data...]
  if (blob_len < 32) {
    log_error("Private key data too short");
    free(blob);
    return -1;
  }

  // Check magic number (OpenSSH private key format)
  if (memcmp(blob, "openssh-key-v1\0", 15) != 0) {
    log_error("Not an OpenSSH private key format");
    free(blob);
    return -1;
  }

  size_t offset = 15; // Skip magic
  if (offset + 4 > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }

  // Read ciphername (should be "none" for unencrypted keys)
  uint32_t ciphername_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + ciphername_len > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }

  // Check if the key is encrypted
  if (ciphername_len != 4 || memcmp(blob + offset, "none", 4) != 0) {
    // Key is encrypted - we need to decrypt it
    char ciphername[256];
    if (ciphername_len >= sizeof(ciphername)) {
      log_error("Cipher name too long");
      free(blob);
      return -1;
    }
    memcpy(ciphername, blob + offset, ciphername_len);
    ciphername[ciphername_len] = '\0';

    log_info("Encrypted private key detected (cipher: %s)", ciphername);

    // Prompt for passphrase
    char passphrase[256];
    if (prompt_ssh_passphrase(passphrase, sizeof(passphrase)) != 0) {
      log_error("Failed to get passphrase");
      free(blob);
      return -1;
    }

    // Try SSH agent first
    char ssh_agent_public_key[1024];
    if (get_ssh_agent_ed25519_key(ssh_agent_public_key, sizeof(ssh_agent_public_key)) == 0) {
      log_info("Using SSH agent for encrypted key operations");
      log_info("SSH agent public key: %.50s...", ssh_agent_public_key);

      // Mark as using SSH agent - we don't need the actual private key
      key_out->type = KEY_TYPE_ED25519;
      // SSH agent will handle the private key operations
      // We can't get the private key from agent, but we can use it for authentication
      memset(key_out->key.ed25519, 0, 32); // Placeholder - agent handles the real key

      sodium_memzero(passphrase, sizeof(passphrase));
      free(blob);
      return 0;
    }

    // SSH agent failed, try to decrypt the key using external tools
    log_info("SSH agent not available, attempting to decrypt key with external tools...");

    // Use ssh-keygen to decrypt the key temporarily
    char temp_key_path[512];
    if (decrypt_key_with_external_tool(path, passphrase, temp_key_path) == 0) {
      log_info("Successfully decrypted key, parsing temporary file...");
      sodium_memzero(passphrase, sizeof(passphrase));
      free(blob);

      // Parse the decrypted key
      int result = parse_private_key(temp_key_path, key_out);

      // Clean up temporary file
      unlink(temp_key_path);

      if (result == 0) {
        log_info("Successfully parsed decrypted SSH key");
        return 0;
      } else {
        log_error("Failed to parse decrypted key");
        return -1;
      }
    } else {
      log_error("Failed to decrypt key with external tools");
      log_error("Please try one of these methods:");
      log_error("1. Add key to SSH agent: ssh-add %s", path);
      log_error("2. Convert to unencrypted: ssh-keygen -p -N \"\" -f %s", path);
      log_error("3. Use an unencrypted Ed25519 key");

      sodium_memzero(passphrase, sizeof(passphrase));
      free(blob);
      return -1;
    }
  }
  offset += ciphername_len;

  // Read kdfname (should be "none")
  if (offset + 4 > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t kdfname_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + kdfname_len > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  offset += kdfname_len;

  // Read kdfoptions (should be empty for "none")
  if (offset + 4 > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t kdfoptions_len =
      (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + kdfoptions_len > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  offset += kdfoptions_len;

  // Read number of keys (should be 1)
  if (offset + 4 > blob_len) {
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

  // Read public key
  if (offset + 4 > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t pubkey_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + pubkey_len > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }

  // Check if it's an Ed25519 public key
  if (pubkey_len < 19 || memcmp(blob + offset, "ssh-ed25519", 11) != 0) {
    log_error("Not an Ed25519 key");
    free(blob);
    return -1;
  }
  offset += pubkey_len;

  // Read private key
  if (offset + 4 > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }
  uint32_t privkey_len = (blob[offset] << 24) | (blob[offset + 1] << 16) | (blob[offset + 2] << 8) | blob[offset + 3];
  offset += 4;
  if (offset + privkey_len > blob_len) {
    log_error("Invalid private key format");
    free(blob);
    return -1;
  }

  // Parse private key data
  if (privkey_len < 19) {
    log_error("Private key data too short");
    free(blob);
    return -1;
  }

  // Check if it's an Ed25519 private key
  if (memcmp(blob + offset, "ssh-ed25519", 11) != 0) {
    log_error("Not an Ed25519 private key");
    free(blob);
    return -1;
  }

  // Skip the key type string and get to the actual key data
  offset += 19; // "ssh-ed25519" + 4 bytes length + 4 bytes string length
  if (offset + 64 > blob_len) {
    log_error("Private key data too short");
    free(blob);
    return -1;
  }

  // Extract the 32-byte Ed25519 private key
  key_out->type = KEY_TYPE_ED25519;
  memcpy(key_out->key.ed25519, blob + offset, 32);

  // Clear sensitive data
  sodium_memzero(blob, blob_len);
  free(blob);

  log_info("Successfully parsed Ed25519 private key from %s", path);
  return 0;
}

// Convert private key to X25519 for DH
int private_key_to_x25519(const private_key_t *key, uint8_t x25519_sk[32]) {
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
  // For now, return a dummy Ed25519 key for testing
  SAFE_MALLOC(*keys_out, sizeof(char *) * 1, char **);
  (*keys_out)[0] = strdup("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... dummy-github-key");
  *num_keys = 1;
  log_info("GitHub key fetching (stub): %s", username);
  return 0;
}

// Fetch SSH keys from GitLab using BearSSL
int fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys) {
  // TODO: Implement BearSSL integration for real HTTPS requests
  // For now, return a dummy Ed25519 key for testing
  SAFE_MALLOC(*keys_out, sizeof(char *) * 1, char **);
  (*keys_out)[0] = strdup("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBar... dummy-gitlab-key");
  *num_keys = 1;
  log_info("GitLab key fetching (stub): %s", username);
  return 0;
}

// Fetch GPG keys from GitHub using BearSSL
int fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys) {
  // TODO: Implement BearSSL integration for real HTTPS requests
  // For now, return a dummy GPG key for testing
  SAFE_MALLOC(*keys_out, sizeof(char *) * 1, char **);
  (*keys_out)[0] =
      strdup("-----BEGIN PGP PUBLIC KEY BLOCK-----\n...dummy-gpg-key...\n-----END PGP PUBLIC KEY BLOCK-----");
  *num_keys = 1;
  log_info("GitHub GPG key fetching (stub): %s", username);
  return 0;
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

// Parse SSH authorized_keys file
int parse_authorized_keys(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  *num_keys = 0;
  char line[2048];

  while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
      continue;

    // Try to parse as SSH key
    if (parse_public_key(line, &keys[*num_keys]) == 0) {
      (*num_keys)++;
    }
  }

  fclose(f);
  return (*num_keys > 0) ? 0 : -1;
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
