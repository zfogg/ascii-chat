/**
 * @file crypto/ssh_agent.c
 * @ingroup crypto
 * @brief 🔌 SSH agent protocol implementation for key authentication via ssh-agent
 */

#include "ssh_agent.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sodium.h>
#include "platform/system.h"
#include "platform/file.h"

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#include <fcntl.h>
#define SAFE_CLOSE _close
#define SAFE_UNLINK _unlink
#define SAFE_POPEN _popen
#define SAFE_PCLOSE _pclose
#else
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#define SAFE_CLOSE close
#define SAFE_UNLINK unlink
#define SAFE_POPEN popen
#define SAFE_PCLOSE pclose
#endif

bool ssh_agent_is_available(void) {
  // Check if SSH_AUTH_SOCK environment variable is set
  const char *auth_sock = SAFE_GETENV("SSH_AUTH_SOCK");
  if (!auth_sock || strlen(auth_sock) == 0) {
    log_debug("ssh-agent not available: SSH_AUTH_SOCK not set");
    return false;
  }

  // Check if socket/pipe exists and is accessible
#ifdef _WIN32
  // On Windows, SSH_AUTH_SOCK points to a named pipe like \\.\pipe\openssh-ssh-agent
  // We can't use access() on named pipes, so just check if the environment variable is set
  // The actual connection will fail later if the agent isn't running
  log_debug("ssh-agent appears available (SSH_AUTH_SOCK=%s)", auth_sock);
#else
  if (access(auth_sock, W_OK) != 0) {
    log_debug("ssh-agent not available: cannot access socket at %s", auth_sock);
    return false;
  }
  log_debug("ssh-agent is available at %s", auth_sock);
#endif

  return true;
}

bool ssh_agent_has_key(const public_key_t *public_key) {
  if (!ssh_agent_is_available()) {
    return false;
  }

  // Use ssh-add -l to list keys
#ifdef _WIN32
  FILE *fp = SAFE_POPEN("ssh-add -l 2>nul", "r");
#else
  FILE *fp = SAFE_POPEN("ssh-add -l 2>/dev/null", "r");
#endif
  if (!fp) {
    log_error("Failed to run ssh-add - OpenSSH may not be installed or not in PATH");
#ifdef _WIN32
    log_error("To install OpenSSH on Windows:");
    log_error("  1. Go to Settings → Apps → Optional Features");
    log_error("  2. Click 'Add a feature' and install 'OpenSSH Client'");
    log_error("  Or download from: https://github.com/PowerShell/Win32-OpenSSH/releases");
#elif defined(__APPLE__)
    log_error("OpenSSH should be pre-installed on macOS.");
    log_error("If ssh-add is not found, ensure /usr/bin is in your PATH.");
    log_error("You can also install the latest version via Homebrew:");
    log_error("  brew install openssh");
#else
    log_error("To install OpenSSH on Linux:");
    log_error("  Debian/Ubuntu: sudo apt-get install openssh-client");
    log_error("  Fedora/RHEL:   sudo dnf install openssh-clients");
    log_error("  Arch Linux:    sudo pacman -S openssh");
    log_error("  Alpine Linux:  sudo apk add openssh-client");
#endif
    return false;
  }

  // Convert public key to hex for comparison
  char key_hex[65];
  for (int i = 0; i < 32; i++) {
    safe_snprintf(key_hex + i * 2, 3, "%02x", public_key->key[i]);
  }

  // Read output and check if our key is listed
  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    // ssh-add -l output format: "256 SHA256:fingerprint comment (ED25519)"
    // We can't easily match without computing the fingerprint, so we'll just
    // return false for now and let the add operation be idempotent
    log_debug("ssh-agent key list: %s", line);
  }

  SAFE_PCLOSE(fp);

  // For now, we don't do exact matching - ssh-add is idempotent anyway
  return false;
}

asciichat_error_t ssh_agent_add_key(const private_key_t *private_key, const char *key_path) {
  if (!ssh_agent_is_available()) {
    return SET_ERRNO(ERROR_CRYPTO, "Cannot add key to ssh-agent: agent not available");
  }

  if (private_key->type != KEY_TYPE_ED25519) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Cannot add key to ssh-agent: only Ed25519 keys supported");
  }

  log_info("Adding key to ssh-agent: %s", key_path ? key_path : "(memory)");

  // Create temporary file with strict permissions
#ifdef _WIN32
  char tmpfile[MAX_PATH];
  if (GetTempPathA(MAX_PATH, tmpfile) == 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to get temp directory");
  }
  if (platform_strlcat(tmpfile, "ascii-chat-key-XXXXXX", MAX_PATH) >= MAX_PATH) {
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Failed to create temporary filename path - buffer overflow");
  }
  if (_mktemp_s(tmpfile, sizeof(tmpfile)) != 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create temporary filename");
  }
  int fd = platform_open(tmpfile, PLATFORM_O_CREAT | PLATFORM_O_EXCL | PLATFORM_O_RDWR, _S_IREAD | _S_IWRITE);
  if (fd < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create temporary file for ssh-agent");
  }
#else
  char tmpfile[] = "/tmp/ascii-chat-key-XXXXXX";
  int fd = mkstemp(tmpfile);
  if (fd < 0) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create temporary file for ssh-agent");
  }

  // Set file permissions to 600 (owner read/write only)
  if (fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
    SAFE_CLOSE(fd);
    SAFE_UNLINK(tmpfile);
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to set permissions on temporary key file");
  }
#endif

  // Write OpenSSH private key format
  FILE *f = platform_fdopen(fd, "w");
  if (!f) {
    SAFE_CLOSE(fd);
    SAFE_UNLINK(tmpfile);
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open temporary file for writing");
  }

  // Write OpenSSH Ed25519 private key format (unencrypted)
  safe_fprintf(f, "-----BEGIN OPENSSH PRIVATE KEY-----\n");

  // Encode key in OpenSSH format
  // For simplicity, we'll write the raw bytes and let ssh-add handle it
  // In practice, we need to write the full OpenSSH private key format
  // This is a simplified version - the full format is more complex

  // Magic header
  const char magic[] = "openssh-key-v1";
  unsigned char buffer[4096];
  size_t pos = 0;

  // Magic string
  memcpy(buffer + pos, magic, strlen(magic) + 1);
  pos += strlen(magic) + 1;

  // Cipher name (none)
  uint32_t len = 4; // "none"
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  memcpy(buffer + pos, "none", 4);
  pos += 4;

  // KDF name (none)
  len = 4; // "none"
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  memcpy(buffer + pos, "none", 4);
  pos += 4;

  // KDF options (empty)
  buffer[pos++] = 0;
  buffer[pos++] = 0;
  buffer[pos++] = 0;
  buffer[pos++] = 0;

  // Number of keys (1)
  buffer[pos++] = 0;
  buffer[pos++] = 0;
  buffer[pos++] = 0;
  buffer[pos++] = 1;

  // Public key section
  size_t pubkey_start = pos;
  // Length placeholder
  pos += 4;

  // Key type string "ssh-ed25519"
  const char *keytype = "ssh-ed25519";
  len = strlen(keytype);
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  SAFE_MEMCPY(buffer + pos, sizeof(buffer) - pos, keytype, len);
  pos += len;

  // Public key data (32 bytes)
  len = 32;
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  SAFE_MEMCPY(buffer + pos, sizeof(buffer) - pos, private_key->public_key, 32);
  pos += 32;

  // Write public key section length
  len = pos - pubkey_start - 4;
  buffer[pubkey_start] = (len >> 24) & 0xFF;
  buffer[pubkey_start + 1] = (len >> 16) & 0xFF;
  buffer[pubkey_start + 2] = (len >> 8) & 0xFF;
  buffer[pubkey_start + 3] = len & 0xFF;

  // Private key section
  size_t privkey_start = pos;
  pos += 4; // Length placeholder

  // Check bytes (random, used for encryption verification)
  uint32_t check = 0x12345678; // Fixed for unencrypted keys
  buffer[pos++] = (check >> 24) & 0xFF;
  buffer[pos++] = (check >> 16) & 0xFF;
  buffer[pos++] = (check >> 8) & 0xFF;
  buffer[pos++] = check & 0xFF;
  buffer[pos++] = (check >> 24) & 0xFF;
  buffer[pos++] = (check >> 16) & 0xFF;
  buffer[pos++] = (check >> 8) & 0xFF;
  buffer[pos++] = check & 0xFF;

  // Key type again
  len = strlen(keytype);
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  SAFE_MEMCPY(buffer + pos, sizeof(buffer) - pos, keytype, len);
  pos += len;

  // Public key again
  len = 32;
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  SAFE_MEMCPY(buffer + pos, sizeof(buffer) - pos, private_key->public_key, 32);
  pos += 32;

  // Private key (64 bytes: 32 seed + 32 public key for Ed25519)
  len = 64;
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  SAFE_MEMCPY(buffer + pos, sizeof(buffer) - pos, private_key->key.ed25519, 64);
  pos += 64;

  // Comment
  const char *comment = private_key->key_comment;
  len = strlen(comment);
  buffer[pos++] = (len >> 24) & 0xFF;
  buffer[pos++] = (len >> 16) & 0xFF;
  buffer[pos++] = (len >> 8) & 0xFF;
  buffer[pos++] = len & 0xFF;
  SAFE_MEMCPY(buffer + pos, sizeof(buffer) - pos, comment, len);
  pos += len;

  // Padding (align to 8 bytes)
  size_t padding_len = 8 - ((pos - privkey_start - 4) % 8);
  for (size_t i = 0; i < padding_len; i++) {
    buffer[pos++] = (unsigned char)(i + 1);
  }

  // Write private key section length
  len = pos - privkey_start - 4;
  buffer[privkey_start] = (len >> 24) & 0xFF;
  buffer[privkey_start + 1] = (len >> 16) & 0xFF;
  buffer[privkey_start + 2] = (len >> 8) & 0xFF;
  buffer[privkey_start + 3] = len & 0xFF;

  // Base64 encode and write
  char *base64;
  base64 = SAFE_MALLOC(pos * 2, char *);
  if (!base64) {
    fclose(f);
    SAFE_UNLINK(tmpfile);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate base64 encoding buffer");
  }

  sodium_bin2base64(base64, pos * 2, buffer, pos, sodium_base64_VARIANT_ORIGINAL);

  // Write in 70-character lines
  size_t base64_len = strlen(base64);
  for (size_t i = 0; i < base64_len; i += 70) {
    size_t line_len = (base64_len - i > 70) ? 70 : (base64_len - i);
    safe_fprintf(f, "%.*s\n", (int)line_len, base64 + i);
  }

  SAFE_FREE(base64);

  safe_fprintf(f, "-----END OPENSSH PRIVATE KEY-----\n");
  (void)fclose(f);

  // Add key to ssh-agent using ssh-add
  char cmd[512];
  safe_snprintf(cmd, sizeof(cmd), "ssh-add %s 2>&1", tmpfile);

  FILE *ssh_add = SAFE_POPEN(cmd, "r");
  if (!ssh_add) {
    sodium_memzero(buffer, sizeof(buffer));
    SAFE_UNLINK(tmpfile);
    SET_ERRNO(ERROR_CRYPTO, "Failed to run ssh-add - OpenSSH may not be installed or not in PATH");
#ifdef _WIN32
    log_error("To install OpenSSH on Windows:");
    log_error("  1. Go to Settings → Apps → Optional Features");
    log_error("  2. Click 'Add a feature' and install 'OpenSSH Client'");
    log_error("  Or download from: https://github.com/PowerShell/Win32-OpenSSH/releases");
#elif defined(__APPLE__)
    log_error("OpenSSH should be pre-installed on macOS.");
    log_error("If ssh-add is not found, ensure /usr/bin is in your PATH.");
    log_error("You can also install the latest version via Homebrew:");
    log_error("  brew install openssh");
#else
    log_error("To install OpenSSH on Linux:");
    log_error("  Debian/Ubuntu: sudo apt-get install openssh-client");
    log_error("  Fedora/RHEL:   sudo dnf install openssh-clients");
    log_error("  Arch Linux:    sudo pacman -S openssh");
    log_error("  Alpine Linux:  sudo apk add openssh-client");
#endif
  }

  // Read output
  char output[256];
  bool success = false;
  while (fgets(output, sizeof(output), ssh_add)) {
    log_debug("ssh-add output: %s", output);
    if (strstr(output, "Identity added")) {
      success = true;
    }
  }

  int ret = SAFE_PCLOSE(ssh_add);

  // Securely delete temporary file
  sodium_memzero(buffer, sizeof(buffer));
  SAFE_UNLINK(tmpfile);

  if (ret == 0 || success) {
    log_info("Successfully added key to ssh-agent");
    return ASCIICHAT_OK;
  } else {
    return SET_ERRNO(ERROR_CRYPTO, "ssh-add returned non-zero exit code: %d", ret);
  }
}
