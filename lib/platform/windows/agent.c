/**
 * @file platform/windows/agent.c
 * @brief Windows SSH/GPG agent socket discovery implementation
 */

#include "../agent.h"
#include "../../common.h"
#include "../../log/logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int platform_get_ssh_agent_socket(char *path_out, size_t path_size) {
  /* Windows: Check SSH_AUTH_SOCK first */
  const char *auth_sock = platform_getenv("SSH_AUTH_SOCK");
  if (auth_sock && strlen(auth_sock) > 0) {
    VALIDATE_AGENT_PATH(auth_sock, path_out, path_size, "SSH_AUTH_SOCK");
  }

  /* Windows default: named pipe for openssh-ssh-agent */
  const char *default_pipe = "\\\\.\\pipe\\openssh-ssh-agent";
  VALIDATE_AGENT_PATH(default_pipe, path_out, path_size, "Default SSH agent pipe");
}

int platform_get_gpg_agent_socket(char *path_out, size_t path_size) {
  /* Windows: Try gpgconf first */
  FILE *fp = _popen("gpgconf --list-dirs agent-socket 2>nul", "r");
  if (fp) {
    if (fgets(path_out, path_size, fp)) {
      /* Remove trailing newline */
      size_t len = strlen(path_out);
      if (len > 0 && path_out[len - 1] == '\n') {
        path_out[len - 1] = '\0';
      }
      _pclose(fp);
      return 0;
    }
    _pclose(fp);
  }

  /* Fallback to default GPG4Win location */
  const char *appdata = platform_getenv("APPDATA");
  if (appdata) {
    size_t needed = strlen(appdata) + strlen("\\gnupg\\S.gpg-agent") + 1;
    if (needed > path_size) {
      log_error("GPG agent socket path too long");
      return -1;
    }
    safe_snprintf(path_out, path_size, "%s\\gnupg\\S.gpg-agent", appdata);
    return 0;
  }

  log_error("Could not determine APPDATA directory");
  return -1;
}
