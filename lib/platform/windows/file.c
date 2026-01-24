/**
 * @file platform/windows/file.c
 * @brief Windows file operations implementation with ACL checking
 * @ingroup platform
 */

#ifdef _WIN32

#include "platform/file.h"
#include "common.h"
#include "log/logging.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdbool.h>

#pragma comment(lib, "advapi32.lib")

asciichat_error_t platform_validate_key_file_permissions(const char *key_path) {
  if (!key_path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
    return ERROR_INVALID_PARAM;
  }

  // Get the file's security descriptor
  PSECURITY_DESCRIPTOR pSecurityDescriptor = NULL;
  PACL pDacl = NULL;
  BOOL daclPresent = FALSE;
  BOOL daclDefaulted = FALSE;

  DWORD result = GetNamedSecurityInfoA((LPSTR)key_path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                       NULL,   // pOwner
                                       NULL,   // pGroup
                                       &pDacl, // pDacl
                                       NULL,   // pSacl
                                       &pSecurityDescriptor);

  if (result != ERROR_SUCCESS) {
    DWORD err = GetLastError();
    log_error("Failed to get file security info: %lu", err);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot read ACL for key file: %s (error %lu)", key_path, err);
  }

  bool is_valid = true;

  // Check if DACL exists and has appropriate entries
  if (pDacl != NULL) {
    ACL_SIZE_INFORMATION aclInfo;
    if (!GetAclInformation(pDacl, &aclInfo, sizeof(aclInfo), AclSizeInformation)) {
      log_error("Failed to get ACL information");
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to validate key file ACL: %s", key_path);
    }

    // Get current user's SID for comparison
    HANDLE processToken = NULL;
    DWORD tokenUserSize = 0;
    PTOKEN_USER pTokenUser = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken)) {
      log_error("Failed to open process token");
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to validate key file ownership: %s", key_path);
    }

    // Get the size needed for token user info
    GetTokenInformation(processToken, TokenUser, NULL, 0, &tokenUserSize);

    pTokenUser = (PTOKEN_USER)LocalAlloc(LMEM_FIXED, tokenUserSize);
    if (!pTokenUser) {
      log_error("Failed to allocate token user buffer");
      CloseHandle(processToken);
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_INVALID_STATE, "Memory allocation failed for ACL validation");
    }

    if (!GetTokenInformation(processToken, TokenUser, pTokenUser, tokenUserSize, &tokenUserSize)) {
      log_error("Failed to get token user information");
      LocalFree(pTokenUser);
      CloseHandle(processToken);
      LocalFree(pSecurityDescriptor);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to get current user info for ACL validation: %s", key_path);
    }

    PSID pCurrentUserSid = pTokenUser->User.Sid;

    // Check each ACE (Access Control Entry) in the DACL
    for (DWORD i = 0; i < aclInfo.AceCount; i++) {
      PACE_HEADER pAceHeader = NULL;
      if (!GetAce(pDacl, i, (LPVOID *)&pAceHeader)) {
        log_error("Failed to get ACE at index %lu", i);
        is_valid = false;
        break;
      }

      // Check ACE type
      if (pAceHeader->AceType == ACCESS_ALLOWED_ACE_TYPE) {
        PACCESS_ALLOWED_ACE pAllowAce = (PACCESS_ALLOWED_ACE)pAceHeader;
        PSID pAceSid = (PSID)&pAllowAce->SidStart;

        // Only the owner (current user) should have access
        if (!EqualSid(pCurrentUserSid, pAceSid)) {
          // Non-owner has access - this is too permissive
          log_error("Key file allows access to non-owner users");
          is_valid = false;
          break;
        }

        // Check that it's only read access, not write/delete
        // For SSH keys, we typically want just READ access (0x00120089)
        // But we allow reasonable key access permissions
        DWORD allowedMask = pAllowAce->Mask;

        // Warn if owner has write permission (which is sometimes OK but not ideal)
        if (allowedMask & (FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD)) {
          log_warn("Key file allows owner to modify/delete the file (consider restricting to read-only)");
        }
      } else if (pAceHeader->AceType == ACCESS_DENIED_ACE_TYPE) {
        // Deny ACEs are fine (they restrict access)
        continue;
      } else if (pAceHeader->AceType == SYSTEM_AUDIT_ACE_TYPE) {
        // Audit ACEs don't affect permissions
        continue;
      } else {
        // Unknown ACE type - be conservative
        log_warn("Unknown ACE type in key file ACL: %d", pAceHeader->AceType);
      }
    }

    LocalFree(pTokenUser);
    CloseHandle(processToken);
  } else {
    // No DACL on the file - this is unusual
    // On Windows, files typically inherit DACL from parent directory
    // This might indicate the file is accessible to everyone
    log_warn("Key file has no DACL (might be accessible to all users)");
    is_valid = false;
  }

  LocalFree(pSecurityDescriptor);

  if (!is_valid) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Key file has overly permissive ACL - ensure only owner can read: %s", key_path);
  }

  return ASCIICHAT_OK;
}

#endif // _WIN32
