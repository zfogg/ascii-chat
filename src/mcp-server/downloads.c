#include "mcp.h"
#include <uv.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

const char *const mcp_output_formats[] = {"png", "jpg",  "jpeg", "bmp", "webp", "gif", "mp4", "mov",
                                          "mkv", "webm", "avi",  "flv", "ogv",  "ogg", "3gp", NULL};

const char *mcp_output_mime(const char *extension) {
  static const char *const mime[] = {"image/png",        "image/jpeg", "image/jpeg",      "image/bmp",
                                     "image/webp",       "image/gif",  "video/mp4",       "video/quicktime",
                                     "video/x-matroska", "video/webm", "video/x-msvideo", "video/x-flv",
                                     "video/ogg",        "video/ogg",  "video/3gpp"};
  for (size_t i = 0; mcp_output_formats[i]; ++i)
    if (!strcmp(extension, mcp_output_formats[i]))
      return mime[i];
  return NULL;
}

// Filenames are generated internally; strict parsing also prevents path traversal.
bool mcp_download_name(const char *name, int64_t *expires) {
  if (!name || strlen(name) < 47)
    return false;
  const char *p = name;
  while (isdigit((unsigned char)*p))
    ++p;
  if (p == name || p - name > 12 || *p++ != '-')
    return false;
  const char *digits_end = p - 1;
  for (int i = 0; i < 32; ++i)
    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')))
      return false;
    else
      ++p;
  if (*p++ != '.')
    return false;
  if (!mcp_output_mime(p))
    return false;
  char number[16];
  size_t n = (size_t)(digits_end - name);
  memcpy(number, name, n);
  number[n] = '\0';
  int64_t value = (int64_t)strtoll(number, NULL, 10);
  if (value <= 0)
    return false;
  if (expires)
    *expires = value;
  return true;
}

void mcp_download_prune(const char *directory, int64_t now) {
  uv_fs_t scan;
  if (uv_fs_scandir(NULL, &scan, directory, 0, NULL) < 0) {
    uv_fs_req_cleanup(&scan);
    return;
  }
  uv_dirent_t entry;
  while (uv_fs_scandir_next(&scan, &entry) == 0) {
    int64_t expires;
    if (!mcp_download_name(entry.name, &expires) || expires > now)
      continue;
    char path[PLATFORM_MAX_PATH_LENGTH];
    int n = safe_snprintf(path, sizeof(path), "%s/%s", directory, entry.name);
    if (n < 0 || (size_t)n >= sizeof(path))
      continue;
    uv_fs_t op;
    uv_fs_unlink(NULL, &op, path, NULL);
    uv_fs_req_cleanup(&op);
  }
  uv_fs_req_cleanup(&scan);
}

asciichat_error_t mcp_download_finalize(const mcp_policy_t *policy, yyjson_mut_doc *doc, int64_t now) {
  yyjson_mut_val *result = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "result");
  yyjson_mut_val *data = yyjson_mut_obj_get(result, "structuredContent");
  yyjson_mut_val *files = yyjson_mut_obj_get(data, "saved_files");
  yyjson_mut_val *first = yyjson_mut_arr_get_first(files);
  const char *old = yyjson_mut_get_str(first);
  if (!old || !*old)
    return ASCIICHAT_OK;
  const char *name = strrchr(old, '/');
  const char *back = strrchr(old, '\\');
  if (back && (!name || back > name))
    name = back;
  name = name ? name + 1 : old;
  int64_t ignored;
  if (!mcp_download_name(name, &ignored))
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid generated download name");
  const char *token = strchr(name, '-');
  int64_t expires = now + 1800;
  char new_name[128], new_path[PLATFORM_MAX_PATH_LENGTH], url[PLATFORM_MAX_PATH_LENGTH * 2];
  int n = safe_snprintf(new_name, sizeof(new_name), "%lld%s", (long long)expires, token);
  if (n < 0 || (size_t)n >= sizeof(new_name))
    return SET_ERRNO(ERROR_INVALID_STATE, "Download name is too long");
  n = safe_snprintf(new_path, sizeof(new_path), "%s/%s", policy->downloads_dir, new_name);
  if (n < 0 || (size_t)n >= sizeof(new_path))
    return SET_ERRNO(ERROR_INVALID_STATE, "Download path is too long");
  const char *prefix = policy->downloads_url_prefix;
  char fallback[128];
  if (!prefix || !*prefix) {
    SAFE_SNPRINTF(fallback, sizeof(fallback), "%s://127.0.0.1:%d",
                  policy->tls_cert && *policy->tls_cert ? "https" : "http", policy->port);
    prefix = fallback;
  }
  size_t prefix_len = strlen(prefix);
  n = safe_snprintf(url, sizeof(url), "%s%sdownloads/%s", prefix, prefix_len && prefix[prefix_len - 1] == '/' ? "" : "/",
                    new_name);
  if (n < 0 || (size_t)n >= sizeof(url))
    return SET_ERRNO(ERROR_INVALID_STATE, "Download URL is too long");
  if (strcmp(old, new_path)) {
    uv_fs_t fs;
    int rc = uv_fs_link(NULL, &fs, old, new_path, NULL);
    uv_fs_req_cleanup(&fs);
    if (rc < 0)
      return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot publish download: %s", uv_strerror(rc));
    uv_fs_unlink(NULL, &fs, old, NULL);
    uv_fs_req_cleanup(&fs);
  }
  time_t t = (time_t)expires;
  struct tm utc;
  char timestamp[32];
  if (platform_gtime(&t, &utc) != ASCIICHAT_OK || !strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc))
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot format download expiration");
  yyjson_mut_arr_clear(files);
  yyjson_mut_arr_add_strcpy(doc, files, url);
  yyjson_mut_obj_add_strcpy(doc, data, "download_url", url);
  yyjson_mut_obj_add_strcpy(doc, data, "expires_at", timestamp);
  yyjson_mut_val *content = yyjson_mut_obj_get(result, "content");
  yyjson_mut_val *item = yyjson_mut_arr_get_first(content);
  char description[sizeof(url) + 80];
  SAFE_SNPRINTF(description, sizeof(description), "Download: %s\nExpires: %s", url, timestamp);
  yyjson_mut_obj_remove_str(item, "text");
  yyjson_mut_obj_add_strcpy(doc, item, "text", description);
  return ASCIICHAT_OK;
}
