#include "mcp.h"
#include <ascii-chat/options/json.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/platform/filesystem.h>
#include <sodium.h>
#include <uv.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

// Exposure is explicit; types, descriptions, enums and CLI parsers remain in the registry.
static const char *const shared_options[] = {"width",
                                             "height",
                                             "color-mode",
                                             "color-filter",
                                             "render-mode",
                                             "palette",
                                             "palette-chars",
                                             "stretch",
                                             "fps",
                                             "snapshot",
                                             "snapshot-delay",
                                             "matrix",
                                             "face-follow",
                                             "flip-x",
                                             "flip-y",
                                             "zalgo",
                                             "utf8",
                                             "strip-ansi",
                                             "loop",
                                             "seek",
                                             "test-pattern",
                                             "test-audio",
                                             "audio",
                                             "audio-spectral-agc",
                                             "audio-pitch-correct",
                                             "spectral-audio",
                                             "audio-spectral-gate",
                                             "audio-multiband",
                                             "audio-adaptive-eq",
                                             "audio-fft-size",
                                             "microphone-volume",
                                             "ivolume",
                                             "speakers-volume",
                                             "volume",
                                             "encode-audio",
                                             "no-encode-audio",
                                             "no-audio-mixer",
                                             "render-theme",
                                             "render-font-size",
                                             NULL};
static const char *const local_options[] = {"render-font", "yt-dlp-options", NULL};
static bool in_names(const char *name, const char *const *names) {
  for (size_t i = 0; names[i]; ++i)
    if (!strcmp(name, names[i]))
      return true;
  return false;
}
static const registry_entry_t *exposed(const mcp_policy_t *policy, const char *name) {
  if (!in_names(name, shared_options) && !(!policy->restricted && in_names(name, local_options)))
    return NULL;
  return registry_find_entry_by_name(name);
}
static yyjson_mut_val *object_schema(yyjson_mut_doc *d) {
  yyjson_mut_val *v = yyjson_mut_obj(d);
  yyjson_mut_obj_add_str(d, v, "type", "object");
  yyjson_mut_obj_add_bool(d, v, "additionalProperties", false);
  yyjson_mut_obj_add_val(d, v, "properties", yyjson_mut_obj(d));
  return v;
}
static void property(yyjson_mut_doc *d, yyjson_mut_val *s, const char *name, const char *type,
                     const char *description) {
  yyjson_mut_val *v = yyjson_mut_obj(d);
  yyjson_mut_obj_add_str(d, v, "type", type);
  yyjson_mut_obj_add_str(d, v, "description", description);
  yyjson_mut_obj_add_val(d, yyjson_mut_obj_get(s, "properties"), name, v);
}
static void enum_strings(yyjson_mut_doc *d, yyjson_mut_val *v, const char *const *values) {
  yyjson_mut_val *a = yyjson_mut_arr(d);
  for (size_t i = 0; values[i]; ++i)
    yyjson_mut_arr_add_str(d, a, values[i]);
  yyjson_mut_obj_add_val(d, v, "enum", a);
}
static yyjson_mut_val *options_schema(yyjson_mut_doc *d, const mcp_policy_t *p) {
  yyjson_mut_val *s = object_schema(d), *props = yyjson_mut_obj_get(s, "properties");
  registry_init_size();
  const char *const *lists[] = {shared_options, local_options};
  for (size_t list = 0; list < (p->restricted ? 1u : 2u); ++list)
    for (size_t i = 0; lists[list][i]; ++i) {
      const registry_entry_t *e = exposed(p, lists[list][i]);
      if (!e)
        continue;
      yyjson_mut_val *v = options_json_schema_for_option(d, e->long_name);
      yyjson_mut_obj_add_val(d, props, e->long_name, v);
      if (!strcmp(e->long_name, "width") || !strcmp(e->long_name, "height")) {
        yyjson_mut_obj_add_int(d, v, "minimum", e->metadata.numeric_range.min);
        yyjson_mut_obj_add_int(d, v, "maximum", !strcmp(e->long_name, "width") ? 512 : 256);
      }
      if (!strcmp(e->long_name, "snapshot"))
        yyjson_mut_obj_add_bool(d, v, "default", true);
      if (!strcmp(e->long_name, "snapshot-delay")) {
        yyjson_mut_obj_add_real(d, v, "minimum", 0);
        yyjson_mut_obj_add_real(d, v, "maximum", 30);
        yyjson_mut_obj_add_real(d, v, "default", 0);
      }
    }
  return s;
}
static void file_properties(yyjson_mut_doc *d, yyjson_mut_val *s, bool local) {
  if (local)
    property(d, s, "path", "string", "Local media path. Mutually exclusive with data_base64.");
  property(d, s, "data_base64", "string",
           "Base64 encoded media bytes; client must read/encode the file. Maximum decoded size 16 MiB.");
  property(d, s, "mime_type", "string", "Media MIME type, e.g. image/png, video/mp4, audio/wav.");
}
yyjson_mut_val *mcp_tools_schema(yyjson_mut_doc *d, const mcp_policy_t *p) {
  const char *names[] = {"render_file", "render_url", "render_webcam", "render_visualization"};
  const char *descs[] = {
      "Convert uploaded media or a local file into ASCII art. Source can be omitted for test-pattern/test-audio.",
      "Convert URL media into ASCII art using the CLI media resolver.",
      "Capture a local webcam as ASCII art; test-pattern avoids camera access.",
      "Render waveform or FFT from test audio, uploaded audio, URL audio, or local audio/microphone."};
  yyjson_mut_val *tools = yyjson_mut_arr(d);
  for (int i = 0; i < 4; ++i) {
    if (p->restricted && i == MCP_WEBCAM)
      continue;
    yyjson_mut_val *tool = yyjson_mut_obj(d), *s = object_schema(d), *props = yyjson_mut_obj_get(s, "properties");
    yyjson_mut_obj_add_str(d, tool, "name", names[i]);
    yyjson_mut_obj_add_str(d, tool, "description", descs[i]);
    yyjson_mut_obj_add_val(d, props, "options", options_schema(d, p));
    property(d, s, "timeout_ms", "integer", "Maximum job time, 1000–60000 ms; default 45000.");
    if (!p->restricted)
      property(d, s, "output_path", "string",
               "Save rendered ASCII art as an image/video, selected by extension. Existing files are never replaced.");
    if (i == MCP_FILE)
      file_properties(d, s, !p->restricted);
    if (i == MCP_URL) {
      property(d, s, "url", "string", "HTTP/HTTPS media URL (local profile also accepts RTSP/RTMP).");
      yyjson_mut_val *required = yyjson_mut_arr(d);
      yyjson_mut_arr_add_str(d, required, "url");
      yyjson_mut_obj_add_val(d, s, "required", required);
    }
    if (i == MCP_WEBCAM) {
      property(d, s, "video_device", "integer", "Local webcam index, 0–10; default 0.");
      yyjson_mut_val *device = yyjson_mut_obj_get(props, "video_device");
      yyjson_mut_obj_add_int(d, device, "default", 0);
      yyjson_mut_obj_add_int(d, device, "minimum", 0);
      yyjson_mut_obj_add_int(d, device, "maximum", 10);
    }
    if (!p->restricted && (i == MCP_WEBCAM || i == MCP_VISUALIZATION)) {
      property(d, s, "audio_device", "integer", "Microphone index; -1 or omitted selects the system default.");
      yyjson_mut_val *device = yyjson_mut_obj_get(props, "audio_device");
      yyjson_mut_obj_add_int(d, device, "default", -1);
      yyjson_mut_obj_add_int(d, device, "minimum", -1);
      yyjson_mut_obj_add_int(d, device, "maximum", INT_MAX);
      yyjson_mut_val *volume = options_json_schema_for_option(d, "microphone-volume");
      yyjson_mut_obj_remove_str(volume, "description");
      yyjson_mut_obj_add_str(d, volume, "description", "Microphone volume multiplier, 0–1; 0 mutes, 1 is normal.");
      yyjson_mut_obj_add_real(d, volume, "default", 1);
      yyjson_mut_obj_add_real(d, volume, "minimum", 0);
      yyjson_mut_obj_add_real(d, volume, "maximum", 1);
      yyjson_mut_obj_add_val(d, props, "volume", volume);
    }
    if (i == MCP_VISUALIZATION) {
      property(d, s, "visualization", "string", "waveform or fft");
      const char *values[] = {"waveform", "fft", NULL};
      enum_strings(d, yyjson_mut_obj_get(props, "visualization"), values);
      yyjson_mut_val *audio = object_schema(d);
      property(d, audio, "type", "string", "Audio source, default test.");
      const char *types[] = {"test", "file", "url", "microphone", NULL};
      const char *remote_types[] = {"test", "file", "url", NULL};
      enum_strings(d, yyjson_mut_obj_get(yyjson_mut_obj_get(audio, "properties"), "type"),
                   p->restricted ? remote_types : types);
      file_properties(d, audio, !p->restricted);
      property(d, audio, "url", "string", "Audio URL.");
      yyjson_mut_obj_add_val(d, props, "audio", audio);
      yyjson_mut_val *req = yyjson_mut_arr(d);
      yyjson_mut_arr_add_str(d, req, "visualization");
      yyjson_mut_obj_add_val(d, s, "required", req);
    }
    yyjson_mut_val *out = object_schema(d);
    property(d, out, "ascii", "string", "Rendered text; saved media may have no terminal text.");
    property(d, out, "elapsed_ms", "integer", "Worker wall-clock duration.");
    yyjson_mut_val *saved = yyjson_mut_obj(d), *items = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, saved, "type", "array");
    yyjson_mut_obj_add_str(d, items, "type", "string");
    yyjson_mut_obj_add_val(d, saved, "items", items);
    yyjson_mut_obj_add_val(d, yyjson_mut_obj_get(out, "properties"), "saved_files", saved);
    yyjson_mut_val *required = yyjson_mut_arr(d);
    yyjson_mut_arr_add_str(d, required, "ascii");
    yyjson_mut_arr_add_str(d, required, "elapsed_ms");
    yyjson_mut_arr_add_str(d, required, "saved_files");
    yyjson_mut_obj_add_val(d, out, "required", required);
    yyjson_mut_obj_add_val(d, tool, "outputSchema", out);
    yyjson_mut_obj_add_val(d, tool, "inputSchema", s);
    yyjson_mut_arr_add_val(tools, tool);
  }
  if (!p->restricted) {
    yyjson_mut_val *tool = yyjson_mut_obj(d);
    yyjson_mut_obj_add_str(d, tool, "name", "device_list");
    yyjson_mut_obj_add_str(d, tool, "description",
                           "List local video and microphone device indices, names, and defaults.");
    yyjson_mut_obj_add_val(d, tool, "inputSchema", object_schema(d));
    yyjson_mut_val *annotations = yyjson_mut_obj(d);
    yyjson_mut_obj_add_bool(d, annotations, "readOnlyHint", true);
    yyjson_mut_obj_add_val(d, tool, "annotations", annotations);
    yyjson_mut_arr_add_val(tools, tool);
  }
  return tools;
}
static asciichat_error_t arg(mcp_render_request_t *r, const char *value) {
  if (r->argc >= 250 || strlen(value) > MCP_MAX_INPUT)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Too many or oversized options");
  r->argv[r->argc++] = platform_strdup(value);
  r->argv[r->argc] = NULL;
  return ASCIICHAT_OK;
}
static asciichat_error_t option(mcp_render_request_t *r, const char *name, const char *value) {
  size_t n = strlen(name) + strlen(value) + 4;
  char *s = SAFE_MALLOC(n, char *);
  SAFE_SNPRINTF(s, n, "--%s=%s", name, value);
  asciichat_error_t err = arg(r, s);
  SAFE_FREE(s);
  const registry_entry_t *entry = registry_find_entry_by_name(name);
  if (err == ASCIICHAT_OK && entry && entry->mode_bitmask == OPTION_MODE_BINARY) {
    size_t last = r->argc - 1;
    for (size_t i = 1; i < last; ++i)
      if (!strcmp(r->argv[i], "mirror")) {
        char *binary = r->argv[last];
        memmove(&r->argv[i + 1], &r->argv[i], (last - i) * sizeof(char *));
        r->argv[i] = binary;
        break;
      }
  }
  return err;
}
static bool enabled(yyjson_val *opts, const char *name) {
  yyjson_val *v = mcp_get(opts, name);
  return v && !(yyjson_is_bool(v) && !yyjson_get_bool(v));
}
static asciichat_error_t check_fields(yyjson_val *obj, const char *const *allowed) {
  if (!yyjson_is_obj(obj))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Arguments must be an object");
  size_t i, max;
  yyjson_val *k, *v;
  yyjson_obj_foreach(obj, i, max, k, v) {
    if (strlen(yyjson_get_str(k)) != yyjson_get_len(k) || !in_names(yyjson_get_str(k), allowed))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown argument: %s", yyjson_get_str(k));
    if (yyjson_is_str(v) && strlen(yyjson_get_str(v)) != yyjson_get_len(v))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Embedded NUL in argument");
  }
  return ASCIICHAT_OK;
}
static asciichat_error_t source_file(const mcp_policy_t *p, yyjson_val *a, mcp_render_request_t *r) {
  yyjson_val *path = mcp_get(a, "path"), *data = mcp_get(a, "data_base64"), *mime = mcp_get(a, "mime_type");
  if ((path != NULL) == (data != NULL))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Provide exactly one of path or data_base64");
  if (path) {
    if (p->restricted)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Local paths are unavailable in server mode");
    if (!yyjson_is_str(path) || !*yyjson_get_str(path) || !strcmp(yyjson_get_str(path), "-") ||
        strstr(yyjson_get_str(path), "://"))
      return SET_ERRNO(ERROR_INVALID_PARAM, "path must be a local file, not stdin or a URL");
    return option(r, "file", yyjson_get_str(path));
  }
  if (!yyjson_is_str(data) || yyjson_get_len(data) == 0 || yyjson_get_len(data) > MCP_MAX_INPUT - 4096)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid or oversized base64 media");
  if (mime && !yyjson_is_str(mime))
    return SET_ERRNO(ERROR_INVALID_PARAM, "mime_type must be a string");
  asciichat_error_t err = platform_mkdtemp(r->upload_dir, sizeof(r->upload_dir), "ascii-mcp-input");
  if (err != ASCIICHAT_OK)
    return err;
  char file[4096];
  SAFE_SNPRINTF(file, sizeof(file), "%s/media", r->upload_dir);
  size_t cap = yyjson_get_len(data) / 4 * 3 + 3, len = 0;
  unsigned char *bytes = SAFE_MALLOC(cap, unsigned char *);
  int rc = sodium_base642bin(bytes, cap, yyjson_get_str(data), yyjson_get_len(data), "\r\n\t ", &len, NULL,
                             sodium_base64_VARIANT_ORIGINAL);
  if (rc || !len || len > 16 * 1024 * 1024) {
    SAFE_FREE(bytes);
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid base64 or media exceeds 16 MiB");
  }
  FILE *f = platform_fopen("mcp-upload", file, "wb");
  if (!f) {
    SAFE_FREE(bytes);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot stage uploaded media");
  }
  bool wrote = fwrite(bytes, 1, len, f) == len;
  if (fclose(f) != 0)
    wrote = false;
  SAFE_FREE(bytes);
  if (!wrote)
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot write uploaded media");
  return option(r, "file", file);
}
static asciichat_error_t source_url(const mcp_policy_t *p, yyjson_val *v, mcp_render_request_t *r) {
  if (!yyjson_is_str(v) || !*yyjson_get_str(v) || yyjson_get_len(v) > 4000)
    return SET_ERRNO(ERROR_INVALID_PARAM, "url must be a nonempty string");
  const char *url = yyjson_get_str(v);
  if (strncmp(url, "https://", 8) && strncmp(url, "http://", 7) &&
      (p->restricted || (strncmp(url, "rtsp://", 7) && strncmp(url, "rtmp://", 7))))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported URL scheme");
  return option(r, "url", url);
}
static asciichat_error_t stage_output(const char *path, mcp_render_request_t *r) {
  if (!path || !*path || strlen(path) > 3500 || !strcmp(path, "-") || strchr(path, '\n'))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid output_path");
  uv_fs_t fs;
  int rc = uv_fs_lstat(NULL, &fs, path, NULL);
  uv_fs_req_cleanup(&fs);
  if (rc >= 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output already exists: %s", path);
  if (rc != UV_ENOENT)
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot inspect output destination");
  char parent[4096];
  SAFE_STRNCPY(parent, path, sizeof(parent));
  char *slash = strrchr(parent, '/'), *back = strrchr(parent, '\\');
  if (back && (!slash || back > slash))
    slash = back;
  const char *filename = path;
  if (slash) {
    filename = path + (slash - parent) + 1;
    if (slash == parent)
      slash[1] = '\0';
    else
      *slash = '\0';
  } else
    SAFE_STRNCPY(parent, ".", sizeof(parent));
  const char *ext = strrchr(filename, '.');
  static const char *const extensions[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp",
                                           ".mp4", ".mkv", ".webm", ".mov", ".avi", NULL};
  if (!ext || !in_names(ext, extensions))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported output extension");
  rc = uv_fs_realpath(NULL, &fs, parent, NULL);
  if (rc < 0) {
    uv_fs_req_cleanup(&fs);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Output parent directory does not exist");
  }
  SAFE_SNPRINTF(r->output_path, sizeof(r->output_path), "%s/%s", (char *)fs.ptr, filename);
  SAFE_SNPRINTF(r->stage_dir, sizeof(r->stage_dir), "%s/.ascii-mcp-XXXXXX", (char *)fs.ptr);
  uv_fs_req_cleanup(&fs);
  rc = uv_fs_mkdtemp(NULL, &fs, r->stage_dir, NULL);
  if (rc < 0) {
    r->stage_dir[0] = '\0';
    uv_fs_req_cleanup(&fs);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot stage output beside destination");
  }
  SAFE_STRNCPY(r->stage_dir, fs.path, sizeof(r->stage_dir));
  uv_fs_req_cleanup(&fs);
  SAFE_SNPRINTF(r->stage_file, sizeof(r->stage_file), "%s/render%s", r->stage_dir, ext);
  return option(r, "render-file", r->stage_file);
}
asciichat_error_t mcp_request_parse(const mcp_policy_t *p, mcp_tool_t tool, yyjson_val *a, mcp_render_request_t *r) {
  memset(r, 0, sizeof(*r));
  r->timeout_ms = 45000;
  r->argv = SAFE_CALLOC(256, sizeof(char *), char **);
  static const char *const fields[] = {"path",         "data_base64",   "mime_type", "url",         "video_device",
                                       "audio",        "visualization", "options",   "output_path", "timeout_ms",
                                       "audio_device", "volume",        NULL};
  asciichat_error_t err = check_fields(a, fields);
  if (err)
    return err;
  const char *source_fields[] = {"path",  "data_base64",   "mime_type",    "url",   "video_device",
                                 "audio", "visualization", "audio_device", "volume"};
  for (size_t i = 0; i < 9; ++i) {
    bool allowed = tool == MCP_FILE ? i < 3 : tool == MCP_URL ? i == 3 : tool == MCP_WEBCAM ? i == 4 || i >= 7 : i >= 5;
    if (!allowed && mcp_get(a, source_fields[i]))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Argument %s is not valid for this tool", source_fields[i]);
  }
  if (p->restricted && (tool == MCP_WEBCAM || mcp_get(a, "path") || mcp_get(a, "output_path") ||
                        mcp_get(a, "audio_device") || mcp_get(a, "volume")))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Local capability is unavailable in server mode");
  yyjson_val *timeout = mcp_get(a, "timeout_ms");
  if (timeout) {
    if (!yyjson_is_int(timeout) || yyjson_get_sint(timeout) < 1000 || yyjson_get_sint(timeout) > 60000)
      return SET_ERRNO(ERROR_INVALID_PARAM, "timeout_ms must be 1000–60000");
    r->timeout_ms = (unsigned)yyjson_get_uint(timeout);
  }
  char exe[4096];
  if (!platform_get_executable_path(exe, sizeof(exe)))
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot locate renderer executable");
  arg(r, exe);
  arg(r, "--log-level=error");
  arg(r, "--no-check-update");
  arg(r, "mirror");
  option(r, "width", "100");
  option(r, "height", "40");
  option(r, "color-mode", "none");
  option(r, "strip-ansi", "true");
  option(r, "snapshot", "true");
  option(r, "snapshot-delay", "0");
  option(r, "splash-screen", "false");
  option(r, "no-audio-playback", "true");
  option(r, "audio", "false");
  yyjson_val *opts = mcp_get(a, "options");
  if (opts && !yyjson_is_obj(opts))
    return SET_ERRNO(ERROR_INVALID_PARAM, "options must be an object");
  size_t i, max;
  yyjson_val *k, *v;
  if (opts)
    yyjson_obj_foreach(opts, i, max, k, v) {
      const char *name = yyjson_get_str(k);
      const registry_entry_t *e = exposed(p, name);
      if (strlen(name) != yyjson_get_len(k) || !e)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported option: %s", name);
      char buf[4097];
      bool omitted = false;
      err = options_json_parse_value(name, v, buf, sizeof(buf), &omitted);
      if (err)
        return err;
      if (omitted)
        continue;
      double n = yyjson_get_num(v);
      if ((!strcmp(name, "width") && (n < e->metadata.numeric_range.min || n > 512)) ||
          (!strcmp(name, "height") && (n < e->metadata.numeric_range.min || n > 256)) ||
          (!strcmp(name, "snapshot-delay") && (n < 0 || n > 30)) || (!strcmp(name, "fps") && (n < 1 || n > 144)))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Option %s is out of bounds", name);
      err = option(r, name, buf);
      if (err)
        return err;
    }
  bool synthetic = enabled(opts, "test-pattern"), test_audio = enabled(opts, "test-audio");
  yyjson_val *audio_device = mcp_get(a, "audio_device"), *volume = mcp_get(a, "volume");
  if (audio_device) {
    if (!yyjson_is_int(audio_device) || yyjson_get_sint(audio_device) < -1 || yyjson_get_sint(audio_device) > INT_MAX)
      return SET_ERRNO(ERROR_INVALID_PARAM, "audio_device must be -1 or a nonnegative device index");
    char index[16];
    SAFE_SNPRINTF(index, sizeof(index), "%d", (int)yyjson_get_sint(audio_device));
    err = option(r, "microphone-index", index);
    if (err)
      return err;
  }
  if (volume) {
    if (!yyjson_is_num(volume) || yyjson_get_num(volume) < 0 || yyjson_get_num(volume) > 1)
      return SET_ERRNO(ERROR_INVALID_PARAM, "volume must be a number from 0 to 1");
    char value[64];
    bool omitted = false;
    err = options_json_parse_value("microphone-volume", volume, value, sizeof(value), &omitted);
    if (err)
      return err;
    if (mcp_get(opts, "microphone-volume") || mcp_get(opts, "ivolume"))
      return SET_ERRNO(ERROR_INVALID_PARAM, "volume conflicts with options.microphone-volume/ivolume");
    err = option(r, "microphone-volume", value);
    if (err)
      return err;
  }
  if (tool == MCP_FILE) {
    if (synthetic || test_audio) {
      if (mcp_get(a, "path") || mcp_get(a, "data_base64"))
        return SET_ERRNO(ERROR_INVALID_PARAM, "Synthetic source conflicts with media");
      if (!synthetic) {
        option(r, "waveform", "true");
        option(r, "audio", "true");
      }
    } else {
      err = source_file(p, a, r);
      if (err)
        return err;
    }
  } else if (tool == MCP_URL) {
    if (synthetic)
      return SET_ERRNO(ERROR_INVALID_PARAM, "test-pattern conflicts with URL");
    err = source_url(p, mcp_get(a, "url"), r);
    if (err)
      return err;
  } else if (tool == MCP_WEBCAM) {
    r->capture_webcam = !synthetic;
    bool record_audio =
        audio_device || volume || enabled(opts, "audio") ||
        (mcp_get(a, "output_path") && yyjson_get_num(mcp_get(opts, "snapshot-delay")) > 0 && !mcp_get(opts, "audio"));
    if (record_audio && mcp_get(opts, "audio") && !enabled(opts, "audio"))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Microphone selection conflicts with options.audio=false");
    if (record_audio && !test_audio) {
      option(r, "audio", "true");
      option(r, "audio-source", "mic");
      option(r, "audio-capture-source", "mic");
    }
    yyjson_val *dev = mcp_get(a, "video_device");
    if (dev) {
      if (!yyjson_is_int(dev) || yyjson_get_sint(dev) < 0 || yyjson_get_sint(dev) > 10)
        return SET_ERRNO(ERROR_INVALID_PARAM, "video_device must be 0–10");
      char b[16];
      SAFE_SNPRINTF(b, sizeof(b), "%d", (int)yyjson_get_sint(dev));
      option(r, "webcam-index", b);
    }
  } else {
    if (synthetic)
      return SET_ERRNO(ERROR_INVALID_PARAM, "test-pattern conflicts with audio visualization");
    yyjson_val *visual = mcp_get(a, "visualization");
    if (!yyjson_is_str(visual) || (strcmp(yyjson_get_str(visual), "waveform") && strcmp(yyjson_get_str(visual), "fft")))
      return SET_ERRNO(ERROR_INVALID_PARAM, "visualization must be waveform or fft");
    option(r, yyjson_get_str(visual), "true");
    option(r, "audio", "true");
    yyjson_val *audio = mcp_get(a, "audio");
    static const char *const audio_fields[] = {"type", "path", "data_base64", "mime_type", "url", NULL};
    if (audio) {
      err = check_fields(audio, audio_fields);
      if (err)
        return err;
    }
    yyjson_val *type = mcp_get(audio, "type");
    if (type && !yyjson_is_str(type))
      return SET_ERRNO(ERROR_INVALID_PARAM, "audio.type must be a string");
    const char *t = type ? yyjson_get_str(type) : (audio_device || volume ? "microphone" : "test");
    if ((audio_device || volume) && strcmp(t, "microphone"))
      return SET_ERRNO(ERROR_INVALID_PARAM, "audio_device and volume require microphone audio");
    if (!strcmp(t, "test")) {
      if (audio && yyjson_obj_size(audio) > 1)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Test audio has no source fields");
      option(r, "test-audio", "true");
      option(r, "audio-source", "all");
    } else if (!strcmp(t, "file")) {
      if (mcp_get(audio, "url") || mcp_get(a, "audio_device") || test_audio)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Conflicting audio sources");
      err = source_file(p, audio, r);
      if (err)
        return err;
      option(r, "audio-source", "media");
      option(r, "audio-capture-source", "media");
    } else if (!strcmp(t, "url")) {
      if (mcp_get(audio, "path") || mcp_get(audio, "data_base64") || mcp_get(a, "audio_device") || test_audio)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Conflicting audio sources");
      err = source_url(p, mcp_get(audio, "url"), r);
      if (err)
        return err;
      option(r, "audio-source", "media");
      option(r, "audio-capture-source", "media");
    } else if (!strcmp(t, "microphone") && !p->restricted) {
      if (mcp_get(audio, "path") || mcp_get(audio, "data_base64") || mcp_get(audio, "url") || test_audio)
        return SET_ERRNO(ERROR_INVALID_PARAM, "Conflicting audio sources");
      option(r, "audio-source", "mic");
      option(r, "audio-capture-source", "mic");
    } else
      return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported audio source");
  }
  if (test_audio)
    option(r, "audio", "true");
  if (tool != MCP_WEBCAM && tool != MCP_VISUALIZATION && !test_audio)
    option(r, "audio-capture-source", "media");
  yyjson_val *output = mcp_get(a, "output_path");
  if (output) {
    if (!yyjson_is_str(output))
      return SET_ERRNO(ERROR_INVALID_PARAM, "output_path must be a string");
    err = stage_output(yyjson_get_str(output), r);
    if (err)
      return err;
  }
  return ASCIICHAT_OK;
}
void mcp_request_destroy(mcp_render_request_t *r) {
  for (size_t i = 0; i < r->argc; ++i)
    SAFE_FREE(r->argv[i]);
  SAFE_FREE(r->argv);
  if (r->upload_dir[0]) {
    asciichat_error_t e = platform_rmdir_recursive(r->upload_dir);
    (void)e;
  }
  if (r->stage_dir[0]) {
    asciichat_error_t e = platform_rmdir_recursive(r->stage_dir);
    (void)e;
  }
  memset(r, 0, sizeof(*r));
}
