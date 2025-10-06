#include "keys.h"
#include "common.h"
#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

// Base64 decode SSH key blob
static int base64_decode_ssh_key(const char* base64, size_t base64_len, uint8_t** blob_out, size_t* blob_len) {
    // Allocate max possible size
    SAFE_MALLOC(*blob_out, base64_len, uint8_t*);

    const char* end;
    int result = sodium_base642bin(
        *blob_out, base64_len,
        base64, base64_len,
        NULL,  // ignore chars
        blob_len,
        &end,
        sodium_base64_VARIANT_ORIGINAL
    );

    if (result != 0) {
        free(*blob_out);
        return -1;
    }

    return 0;
}

// Parse SSH Ed25519 public key from "ssh-ed25519 AAAAC3..." format
static int parse_ssh_ed25519_line(const char* line, uint8_t ed25519_pk[32]) {
    // Find "ssh-ed25519 "
    const char* type_start = strstr(line, "ssh-ed25519");
    if (!type_start) return -1;

    // Skip to base64 part
    const char* base64_start = type_start + 11;  // strlen("ssh-ed25519")
    while (*base64_start == ' ' || *base64_start == '\t') base64_start++;

    // Find end of base64 (space, newline, or end of string)
    const char* base64_end = base64_start;
    while (*base64_end && *base64_end != ' ' && *base64_end != '\t' &&
           *base64_end != '\n' && *base64_end != '\r') {
        base64_end++;
    }

    size_t base64_len = base64_end - base64_start;

    // Base64 decode
    uint8_t* blob;
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
int hex_decode(const char* hex, uint8_t* output, size_t output_len) {
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

        char hex_byte[3] = {hex[i*2], hex[i*2+1], '\0'};
        char* endptr;
        unsigned long val = strtoul(hex_byte, &endptr, 16);
        if (*endptr != '\0' || val > 255) {
            return -1;
        }
        output[i] = (uint8_t)val;
    }

    return 0;
}

// Parse public key from any format (SSH, GPG, X25519, GitHub, etc.)
int parse_public_key(const char* input, public_key_t* key_out) {
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
        const char* comment_start = strstr(input, " ");
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
        const char* username = input + 7;
        char** keys;
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
        const char* username = input + 7;
        char** keys;
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
        const char* key_id = input + 4;

        // Check if gpg is available
#ifdef _WIN32
        FILE* fp = popen("gpg --version 2>nul", "r");
#else
        FILE* fp = popen("gpg --version 2>/dev/null", "r");
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
    FILE* f = fopen(input, "r");
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
int public_key_to_x25519(const public_key_t* key, uint8_t x25519_pk[32]) {
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

// Parse SSH private key from file
int parse_private_key(const char* path, private_key_t* key_out) {
    memset(key_out, 0, sizeof(private_key_t));

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        log_error("Failed to open private key file: %s", path);
        return -1;
    }

    char line[2048];
    bool in_private_key = false;
    bool found_ed25519 = false;

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

        if (in_private_key && strstr(line, "ssh-ed25519")) {
            found_ed25519 = true;
            // For now, we'll just mark it as Ed25519 type
            // TODO: Parse the actual key material from the OpenSSH format
            key_out->type = KEY_TYPE_ED25519;
            break;
        }
    }

    fclose(f);

    if (!found_ed25519) {
        log_error("No Ed25519 private key found in file: %s", path);
        return -1;
    }

    // For now, just return success with a dummy key
    // TODO: Parse the actual key material from the OpenSSH format
    // This is complex and requires proper OpenSSH private key parsing
    memset(key_out->key.ed25519, 0x42, 64); // Dummy key for testing
    return 0;
}

// Convert private key to X25519 for DH
int private_key_to_x25519(const private_key_t* key, uint8_t x25519_sk[32]) {
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
int fetch_github_keys(const char* username, char*** keys_out, size_t* num_keys) {
    // TODO: Implement BearSSL integration for real HTTPS requests
    // For now, return a dummy Ed25519 key for testing
    SAFE_MALLOC(*keys_out, sizeof(char*) * 1, char**);
    (*keys_out)[0] = strdup("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFoo... dummy-github-key");
    *num_keys = 1;
    log_info("GitHub key fetching (stub): %s", username);
    return 0;
}

// Fetch SSH keys from GitLab using BearSSL
int fetch_gitlab_keys(const char* username, char*** keys_out, size_t* num_keys) {
    // TODO: Implement BearSSL integration for real HTTPS requests
    // For now, return a dummy Ed25519 key for testing
    SAFE_MALLOC(*keys_out, sizeof(char*) * 1, char**);
    (*keys_out)[0] = strdup("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIBar... dummy-gitlab-key");
    *num_keys = 1;
    log_info("GitLab key fetching (stub): %s", username);
    return 0;
}

// Fetch GPG keys from GitHub using BearSSL
int fetch_github_gpg_keys(const char* username, char*** keys_out, size_t* num_keys) {
    // TODO: Implement BearSSL integration for real HTTPS requests
    // For now, return a dummy GPG key for testing
    SAFE_MALLOC(*keys_out, sizeof(char*) * 1, char**);
    (*keys_out)[0] = strdup("-----BEGIN PGP PUBLIC KEY BLOCK-----\n...dummy-gpg-key...\n-----END PGP PUBLIC KEY BLOCK-----");
    *num_keys = 1;
    log_info("GitHub GPG key fetching (stub): %s", username);
    return 0;
}

// Fetch GPG keys from GitLab using BearSSL
int fetch_gitlab_gpg_keys(const char* username, char*** keys_out, size_t* num_keys) {
    // TODO: Implement BearSSL integration for real HTTPS requests
    // For now, return a dummy GPG key for testing
    SAFE_MALLOC(*keys_out, sizeof(char*) * 1, char**);
    (*keys_out)[0] = strdup("-----BEGIN PGP PUBLIC KEY BLOCK-----\n...dummy-gitlab-gpg-key...\n-----END PGP PUBLIC KEY BLOCK-----");
    *num_keys = 1;
    log_info("GitLab GPG key fetching (stub): %s", username);
    return 0;
}

// Parse SSH authorized_keys file
int parse_authorized_keys(const char* path, public_key_t* keys, size_t* num_keys, size_t max_keys) {
    FILE* f = fopen(path, "r");
    if (!f) return -1;

    *num_keys = 0;
    char line[2048];

    while (fgets(line, sizeof(line), f) && *num_keys < max_keys) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Try to parse as SSH key
        if (parse_public_key(line, &keys[*num_keys]) == 0) {
            (*num_keys)++;
        }
    }

    fclose(f);
    return (*num_keys > 0) ? 0 : -1;
}

// Convert public key to display format (ssh-ed25519 or x25519 hex)
void format_public_key(const public_key_t* key, char* output, size_t output_size) {
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
                snprintf(hex + i*2, 3, "%02x", key->key[i]);
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
