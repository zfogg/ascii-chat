#import <Foundation/Foundation.h>

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/memory.h>
#include <string.h>

asciichat_error_t platform_load_system_ca_certs(char **pem_data_out, size_t *pem_size_out) {
  if (!pem_data_out || !pem_size_out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "CA output pointers cannot be NULL");

  *pem_data_out = NULL;
  *pem_size_out = 0;
  NSURL *url = [[NSBundle mainBundle] URLForResource:@"ca-certificates" withExtension:@"pem"];
  NSData *encoded = url ? [NSData dataWithContentsOfURL:url] : nil;
  if (!encoded || encoded.length == 0)
    return SET_ERRNO(ERROR_NOT_FOUND, "The iOS app bundle does not contain ca-certificates.pem");
  char *result = SAFE_MALLOC(encoded.length + 1, char *);
  memcpy(result, encoded.bytes, encoded.length);
  result[encoded.length] = '\0';
  *pem_data_out = result;
  *pem_size_out = encoded.length;
  return ASCIICHAT_OK;
}
