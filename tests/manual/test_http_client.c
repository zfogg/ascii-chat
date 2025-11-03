/**
 * @file test_http_client.c
 * @brief Manual test for HTTPS client and key fetching
 *
 * Run with: ./build/bin/test_http_client
 */

#include "common.h"
#include "crypto/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_github_ssh_keys(const char *username) {
  printf("\n=== Testing GitHub SSH Keys for: %s ===\n", username);

  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_github_ssh_keys(username, &keys, &num_keys);

  if (result == 0) {
    printf("✓ Successfully fetched %zu Ed25519 SSH key(s)\n\n", num_keys);

    for (size_t i = 0; i < num_keys; i++) {
      printf("SSH Key %zu: %.80s...\n", i + 1, keys[i]);
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    printf("✗ Failed to fetch SSH keys\n");
  }
}

static void test_github_gpg_keys(const char *username) {
  printf("\n=== Testing GitHub GPG Keys for: %s ===\n", username);

  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_github_gpg_keys(username, &keys, &num_keys);

  if (result == 0) {
    printf("✓ Successfully fetched %zu GPG key(s)\n\n", num_keys);

    for (size_t i = 0; i < num_keys; i++) {
      // Show first few lines of each GPG key
      const char *line = keys[i];
      int line_count = 0;
      printf("GPG Key %zu:\n", i + 1);
      while (*line && line_count < 5) {
        const char *line_end = line;
        while (*line_end && *line_end != '\n') {
          line_end++;
        }
        printf("  %.*s\n", (int)(line_end - line), line);
        if (*line_end == '\n') {
          line_end++;
        }
        line = line_end;
        line_count++;
      }
      printf("  ... (%zu total bytes)\n", strlen(keys[i]));
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    printf("✗ Failed to fetch GPG keys\n");
  }
}

static void test_gitlab_ssh_keys(const char *username) {
  printf("\n=== Testing GitLab SSH Keys for: %s ===\n", username);

  char **keys = NULL;
  size_t num_keys = 0;

  int result = fetch_gitlab_ssh_keys(username, &keys, &num_keys);

  if (result == 0) {
    printf("✓ Successfully fetched %zu Ed25519 SSH key(s)\n\n", num_keys);

    for (size_t i = 0; i < num_keys; i++) {
      printf("SSH Key %zu: %.80s...\n", i + 1, keys[i]);
      SAFE_FREE(keys[i]);
    }
    SAFE_FREE(keys);
  } else {
    printf("✗ Failed to fetch SSH keys (user may not have Ed25519 keys)\n");
  }
}

static void test_https_get(void) {
  printf("\n=== Testing Basic HTTPS GET ===\n");

  char *response = https_get("api.github.com", "/zen");

  if (response) {
    printf("✓ HTTPS GET successful\n");
    printf("Response: %s\n", response);
    SAFE_FREE(response);
  } else {
    printf("✗ HTTPS GET failed\n");
  }
}

int main(int argc, char *argv[]) {
  const char *username = "zfogg"; // Default test user

  if (argc > 1) {
    username = argv[1];
  }

  printf("ascii-chat HTTPS Client Test\n");
  printf("============================\n");
  printf("Testing with username: %s\n", username);

  // Test basic HTTPS
  test_https_get();

  // Test SSH key fetching
  test_github_ssh_keys(username);
  test_gitlab_ssh_keys(username);

  // Test GPG key fetching
  test_github_gpg_keys(username);

  printf("\n=== All Tests Complete ===\n");

  return 0;
}
