/**
 * @file platform/posix/agent.c
 * @brief POSIX SSH/GPG agent socket discovery implementation
 * @ingroup platform
 */

#include <ascii-chat/platform/agent.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

int platform_get_ssh_agent_socket(char *path_out, size_t path_size) {
  /* Unix: SSH_AUTH_SOCK is required */
  const char *auth_sock = platform_getenv("SSH_AUTH_SOCK");
  VALIDATE_AGENT_PATH(auth_sock, path_out, path_size, "SSH_AUTH_SOCK");
}

int platform_get_gpg_agent_socket(char *path_out, size_t path_size) {
  /* Try gpgconf first */
  FILE *fp = popen("gpgconf --list-dirs agent-socket 2>/dev/null", "r");
  if (fp) {
    if (fgets(path_out, path_size, fp)) {
      /* Remove trailing newline */
      size_t len = strlen(path_out);
      if (len > 0 && path_out[len - 1] == '\n') {
        path_out[len - 1] = '\0';
      }
      pclose(fp);
      return 0;
    }
    pclose(fp);
  }

  /* Fallback: ~/.gnupg/S.gpg-agent */
  const char *home = platform_getenv("HOME");
  if (home && home[0] != '\0') {
    size_t needed = strlen(home) + strlen("/.gnupg/S.gpg-agent") + 1;
    if (needed > path_size) {
      log_error("GPG agent socket path too long");
      return -1;
    }
    safe_snprintf(path_out, path_size, "%s/.gnupg/S.gpg-agent", home);
    return 0;
  }

  log_error("Could not determine GPG agent socket path");
  return -1;
}
