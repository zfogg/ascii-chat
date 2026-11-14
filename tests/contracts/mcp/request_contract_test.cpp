#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/rcu.h>
#include "../../../src/mcp-server/mcp.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/video/webcam/webcam.h>
#include "../../../src/mcp-server/request.c"
static bool devices_fail;
static asciichat_error_t fixture_cameras(webcam_device_info_t **devices, unsigned *count) {
  if (devices_fail)
    return SET_ERRNO(ERROR_WEBCAM, "Fixture camera unavailable");
  *count = 2;
  *devices = SAFE_CALLOC(2, sizeof(**devices), webcam_device_info_t *);
  (*devices)[0].index = 0;
  (*devices)[1].index = 4;
  SAFE_STRNCPY((*devices)[0].name, "Default camera", sizeof((*devices)[0].name));
  SAFE_STRNCPY((*devices)[1].name, "Other camera", sizeof((*devices)[1].name));
  return ASCIICHAT_OK;
}
static asciichat_error_t fixture_microphones(audio_device_info_t **devices, unsigned *count) {
  if (devices_fail)
    return ERROR_AUDIO;
  *count = 2;
  *devices = SAFE_CALLOC(2, sizeof(**devices), audio_device_info_t *);
  (*devices)[0].index = 2;
  (*devices)[0].is_default_input = true;
  (*devices)[1].index = 6;
  SAFE_STRNCPY((*devices)[0].name, "Default microphone", sizeof((*devices)[0].name));
  return ASCIICHAT_OK;
}
static void fixture_camera_free(webcam_device_info_t *devices) {
  SAFE_FREE(devices);
}
static void fixture_audio_free(audio_device_info_t *devices) {
  SAFE_FREE(devices);
}
static int fixture_worker(platform_thread_t *thread, const char *name, void *(*fn)(void *), void *data) {
  (void)thread;
  (void)name;
  (void)fn;
  (void)data;
  return -1;
}
void http_renderer_process(http_renderer_process_request_t *request, atomic_t *cancelled, http_renderer_process_result_t *result) {
  (void)request;
  (void)cancelled;
  SAFE_STRNCPY(result->error, "No renderer requested by this contract", sizeof(result->error));
}
void mcp_http_wake(mcp_server_t *server) {
  (void)server;
}
void mcp_stdio_send(mcp_server_t *server, yyjson_mut_doc *response) {
  (void)server;
  (void)response;
}
#define webcam_list_devices fixture_cameras
#define audio_list_input_devices fixture_microphones
#define webcam_free_device_list fixture_camera_free
#define audio_free_device_list fixture_audio_free
#define platform_thread_create fixture_worker
#include "../../../src/mcp-server/protocol.c"
#undef webcam_list_devices
#undef audio_list_input_devices
#undef webcam_free_device_list
#undef audio_free_device_list
#undef platform_thread_create
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  errno_suppress(true);
  ASCIICHAT_REQUIRE_EQ(options_state_init(), ASCIICHAT_OK);
  registry_init_size();
}
static void teardown(void) {
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
static bool has_arg(const http_renderer_process_request_t *request, const char *value) {
  for (size_t i = 0; i < request->argc; ++i)
    if (!strcmp(request->argv[i], value))
      return true;
  return false;
}
static yyjson_doc *read_json(const char *text) {
  yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
  ASCIICHAT_REQUIRE_NOT_NULL(doc);
  return doc;
}
TEST_CASE("mcp_requests/malformed_arguments_and_incompatible_sources_are_rejected_and_partial_allocations_are_released", "[mcp_requests]") {
  const struct {
    mcp_tool_t tool;
    const char *json;
  } cases[] = {
      {MCP_FILE, "[]"},
      {MCP_FILE, "{\"unknown\":true}"},
      {MCP_FILE, "{\"path\":2}"},
      {MCP_FILE, "{\"path\":\"\"}"},
      {MCP_FILE, "{\"path\":\"-\"}"},
      {MCP_FILE, "{\"path\":\"http://localhost/a\"}"},
      {MCP_FILE, "{\"data_base64\":\"\"}"},
      {MCP_FILE, "{\"data_base64\":123}"},
      {MCP_FILE, "{\"data_base64\":\"@@@\"}"},
      {MCP_FILE, "{\"data_base64\":\"AA==\",\"mime_type\":4}"},
      {MCP_FILE, "{\"path\":\"a\",\"data_base64\":\"AA==\"}"},
      {MCP_FILE, "{\"options\":[]}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"width\":513}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"height\":257}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"fps\":-1}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"fps\":145}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"snapshot-delay\":31}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"snapshot-delay\":-1}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"bad-flag\":true}}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true},\"timeout_ms\":999}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true},\"timeout_ms\":60001}"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true},\"timeout_ms\":\"1000\"}"},
      {MCP_URL, "{\"url\":\"\"}"},
      {MCP_URL, "{\"url\":34}"},
      {MCP_URL, "{\"url\":\"ftp://localhost/a\"}"},
      {MCP_URL, "{\"url\":\"http://localhost/a\",\"options\":{\"test-pattern\":true}}"},
      {MCP_WEBCAM, "{\"video_device\":-1}"},
      {MCP_WEBCAM, "{\"video_device\":11}"},
      {MCP_WEBCAM, "{\"audio_device\":-2}"},
      {MCP_WEBCAM, "{\"audio_device\":2147483648}"},
      {MCP_WEBCAM, "{\"audio_device\":\"1\"}"},
      {MCP_WEBCAM, "{\"volume\":-0.1}"},
      {MCP_WEBCAM, "{\"volume\":1.1}"},
      {MCP_WEBCAM, "{\"volume\":\"1\"}"},
      {MCP_WEBCAM, "{\"volume\":0.5,\"options\":{\"microphone-volume\":0.5}}"},
      {MCP_WEBCAM, "{\"audio_device\":0,\"options\":{\"audio\":false}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"bad\"}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":[]}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":1}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":\"test\",\"path\":\"x\"}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":\"file\",\"url\":\"http://localhost\"}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":\"url\",\"path\":\"x\"}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":\"microphone\",\"path\":\"x\"}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio_device\":0,\"audio\":{\"type\":\"test\"}}"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":\"unsupported\"}}"},
      {MCP_FILE, "{\"path\":\"x\",\"options\":{\"test-audio\":true}}"},
      {MCP_FILE, "{\"url\":\"http://localhost/a\"}"},
      {MCP_FILE, "{\"path\":\"x\",\"output_path\":true}"},
      {MCP_FILE, "{\"path\":\"x\",\"output_path\":\"-\"}"},
      {MCP_FILE, "{\"path\":\"x\",\"output_path\":\"invalid\\nname.png\"}"},
      {MCP_FILE, "{\"path\":\"x\",\"output_path\":\"unsupported.exe\"}"},
  };
  mcp_policy_t policy = {0};
  for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
    yyjson_doc *doc = read_json(cases[i].json);
    http_renderer_process_request_t request;
    ASCIICHAT_REQUIRE_NE(mcp_request_parse(&policy, cases[i].tool, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK,
                  "case %u: %s", i, cases[i].json);
    mcp_request_destroy(&request);
    ASCIICHAT_REQUIRE_EQ(request.argc, 0);
    ASCIICHAT_REQUIRE_NULL(request.argv);
    ASCIICHAT_REQUIRE_EQ(request.upload_dir[0], 0);
    yyjson_doc_free(doc);
    CLEAR_ERRNO_ALL();
  }
}

TEST_CASE("mcp_requests/accepted_sources_produce_bounded_cli_arguments_and_staged_uploads_are_destroyed", "[mcp_requests]") {
  const struct {
    mcp_tool_t tool;
    const char *json;
    const char *expected;
  } cases[] = {
      {MCP_FILE, "{\"path\":\"local.ppm\"}", "--file=local.ppm"},
      {MCP_FILE, "{\"data_base64\":\"AA==\",\"mime_type\":\"image/png\"}", "--audio-capture-source=media"},
      {MCP_FILE, "{\"options\":{\"test-audio\":true}}", "--waveform=true"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true},\"timeout_ms\":1000}", "--test-pattern=0"},
      {MCP_URL, "{\"url\":\"http://127.0.0.1/frame\"}", "--url=http://127.0.0.1/frame"},
      {MCP_URL, "{\"url\":\"https://localhost/frame\"}", "--url=https://localhost/frame"},
      {MCP_URL, "{\"url\":\"rtsp://localhost/frame\"}", "--url=rtsp://localhost/frame"},
      {MCP_URL, "{\"url\":\"rtmp://localhost/frame\"}", "--url=rtmp://localhost/frame"},
      {MCP_WEBCAM, "{\"video_device\":2,\"audio_device\":0,\"volume\":0.25}", "--webcam-index=2"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio\":{\"type\":\"file\",\"data_base64\":\"AA==\"}}",
       "--audio-capture-source=media"},
      {MCP_VISUALIZATION,
       "{\"visualization\":\"waveform\",\"audio\":{\"type\":\"url\",\"url\":\"http://localhost/audio\"}}",
       "--url=http://localhost/audio"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\",\"audio_device\":-1,\"volume\":0.5}",
       "--audio-capture-source=mic"},
      {MCP_VISUALIZATION, "{\"visualization\":\"fft\"}", "--test-audio=true"},
  };
  mcp_policy_t policy = {0};
  for (unsigned i = 0; i < ARRAY_SIZE(cases); ++i) {
    yyjson_doc *doc = read_json(cases[i].json);
    http_renderer_process_request_t request;
    ASCIICHAT_REQUIRE_EQ(mcp_request_parse(&policy, cases[i].tool, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK,
                 "case %u: %s", i, cases[i].json);
    ASCIICHAT_REQUIRE(has_arg(&request, cases[i].expected), "case %u missing %s", i, cases[i].expected);
    ASCIICHAT_REQUIRE_NULL(request.argv[request.argc]);
    char upload[PLATFORM_MAX_PATH_LENGTH];
    SAFE_STRNCPY(upload, request.upload_dir, sizeof(upload));
    if (upload[0]) {
      char path[PLATFORM_MAX_PATH_LENGTH];
      SAFE_SNPRINTF(path, sizeof(path), "%s/media", upload);
      FILE *f = platform_fopen("uploaded-byte", path, "rb");
      ASCIICHAT_REQUIRE_NOT_NULL(f);
      ASCIICHAT_REQUIRE_EQ(fgetc(f), 0);
      ASCIICHAT_REQUIRE_EQ(fgetc(f), EOF);
      fclose(f);
    }
    mcp_request_destroy(&request);
    if (upload[0])
      ASCIICHAT_REQUIRE_NE(platform_access(upload, PLATFORM_ACCESS_EXISTS), 0);
    yyjson_doc_free(doc);
    CLEAR_ERRNO_ALL();
  }
}
TEST_CASE("mcp_requests/binary_options_are_placed_before_mode_and_argument_overflow_preserves_existing_arguments", "[mcp_requests]") {
  http_renderer_process_request_t request = {.argv = SAFE_CALLOC(MCP_REQUEST_ARGV_CAPACITY, sizeof(char *), char **)};
  ASCIICHAT_REQUIRE_EQ(arg(&request, "ascii-chat"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(arg(&request, "mirror"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(option(&request, "log-level", "debug"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(request.argv[1], "--log-level=debug");
  ASCIICHAT_REQUIRE_STR_EQ(request.argv[2], "mirror");
  while (request.argc < MCP_REQUEST_ARGV_CAPACITY - MCP_REQUEST_RESERVED_ARGS)
    ASCIICHAT_REQUIRE_EQ(arg(&request, "--snapshot=true"), ASCIICHAT_OK);
  size_t count = request.argc;
  ASCIICHAT_REQUIRE_EQ(arg(&request, "overflow"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(request.argc, count);
  ASCIICHAT_REQUIRE_NULL(request.argv[count]);
  mcp_request_destroy(&request);
}
TEST_CASE("mcp_requests/output_staging_does_not_create_or_replace_the_published_destination", "[mcp_requests]") {
  char directory[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "mcp-stage-contract"), ASCIICHAT_OK);
  char output[PLATFORM_MAX_PATH_LENGTH];
  SAFE_SNPRINTF(output, sizeof(output), "%s/picture.png", directory);
  http_renderer_process_request_t request = {.argv = SAFE_CALLOC(MCP_REQUEST_ARGV_CAPACITY, sizeof(char *), char **)};
  ASCIICHAT_REQUIRE_EQ(stage_output(output, &request), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(platform_access(output, PLATFORM_ACCESS_EXISTS), 0);
  ASCIICHAT_REQUIRE_EQ(platform_access(request.stage_dir, PLATFORM_ACCESS_EXISTS), 0);
  char stage[PLATFORM_MAX_PATH_LENGTH];
  SAFE_STRNCPY(stage, request.stage_dir, sizeof(stage));
  mcp_request_destroy(&request);
  ASCIICHAT_REQUIRE_NE(platform_access(stage, PLATFORM_ACCESS_EXISTS), 0);
  FILE *f = platform_fopen("existing-output", output, "wb");
  ASCIICHAT_REQUIRE_NOT_NULL(f);
  ASCIICHAT_REQUIRE_EQ(fwrite("keep", 1, 4, f), 4);
  ASCIICHAT_REQUIRE_EQ(fclose(f), 0);
  request = (http_renderer_process_request_t){.argv = SAFE_CALLOC(MCP_REQUEST_ARGV_CAPACITY, sizeof(char *), char **)};
  ASCIICHAT_REQUIRE_EQ(stage_output(output, &request), ERROR_INVALID_PARAM);
  mcp_request_destroy(&request);
  f = platform_fopen("preserved-output", output, "rb");
  ASCIICHAT_REQUIRE_NOT_NULL(f);
  char bytes[4];
  ASCIICHAT_REQUIRE_EQ(fread(bytes, 1, 4, f), 4);
  ASCIICHAT_REQUIRE_ARRAY_EQ(bytes, "keep", 4);
  fclose(f);
  ASCIICHAT_REQUIRE_EQ(platform_unlink(output), 0);
  ASCIICHAT_REQUIRE_EQ(platform_rmdir_recursive(directory), ASCIICHAT_OK);
}
static yyjson_mut_doc *dispatch_text(mcp_server_t *server, const char *json, void *connection) {
  yyjson_doc *doc = read_json(json);
  bool pending = true;
  yyjson_mut_doc *response = mcp_dispatch(server, doc, connection, NULL, &pending);
  ASCIICHAT_REQUIRE_NOT(pending);
  yyjson_doc_free(doc);
  return response;
}
static void expect_rpc_error(mcp_server_t *server, const char *json, int code) {
  yyjson_mut_doc *response = dispatch_text(server, json, NULL);
  ASCIICHAT_REQUIRE_NOT_NULL(response);
  yyjson_mut_val *error = yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "error");
  ASCIICHAT_REQUIRE_NOT_NULL(error);
  ASCIICHAT_REQUIRE_EQ(yyjson_mut_get_sint(yyjson_mut_obj_get(error, "code")), code);
  yyjson_mut_doc_free(response);
  CLEAR_ERRNO_ALL();
}

TEST_CASE("mcp_requests/protocol_envelopes_reject_invalid_types_and_worker_start_failure_releases_job", "[mcp_requests]") {
  mcp_job_t jobs[1] = {0};
  mcp_server_t server = {.policy = {.max_jobs = 1}, .jobs = jobs};
  expect_rpc_error(&server, "[]", -32600);
  expect_rpc_error(&server, "{\"jsonrpc\":\"1.0\",\"method\":\"ping\",\"id\":1}", -32600);
  expect_rpc_error(&server, "{\"jsonrpc\":\"2.0\",\"method\":1,\"id\":1}", -32600);
  expect_rpc_error(&server, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":false}", -32600);
  expect_rpc_error(&server, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":null}", -32600);
  expect_rpc_error(&server, "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"params\":[],\"id\":1}", -32602);
  expect_rpc_error(&server, "{\"jsonrpc\":\"2.0\",\"method\":\"unknown\",\"id\":1}", -32601);
  expect_rpc_error(
      &server, "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"unknown\"},\"id\":1}", -32602);
  expect_rpc_error(&server,
                   "{\"jsonrpc\":\"2.0\",\"method\":\"tools/"
                   "call\",\"params\":{\"name\":\"device_list\",\"arguments\":{\"unknown\":1}},\"id\":1}",
                   -32602);
  expect_rpc_error(
      &server,
      "{\"jsonrpc\":\"2.0\",\"method\":\"tools/"
      "call\",\"params\":{\"name\":\"render_file\",\"arguments\":{\"options\":{\"test-pattern\":true}}},\"id\":1}",
      -32603);
  ASCIICHAT_REQUIRE_NOT(server.jobs[0].started);
  ASCIICHAT_REQUIRE_NULL(server.jobs[0].message);
  ASCIICHAT_REQUIRE_NULL(server.jobs[0].request.argv);
}

TEST_CASE("mcp_requests/device_results_include_defaults_and_report_partial_enumeration_failure", "[mcp_requests]") {
  yyjson_mut_doc *response = device_list_result(NULL, NULL);
  yyjson_mut_val *root = yyjson_mut_doc_get_root(response), *result = yyjson_mut_obj_get(root, "result"),
                 *data = yyjson_mut_obj_get(result, "structuredContent");
  ASCIICHAT_REQUIRE_NOT_NULL(data);
  ASCIICHAT_REQUIRE_EQ(yyjson_mut_arr_size(yyjson_mut_obj_get(data, "video_devices")), 2);
  ASCIICHAT_REQUIRE_EQ(yyjson_mut_arr_size(yyjson_mut_obj_get(data, "audio_devices")), 2);
  ASCIICHAT_REQUIRE_EQ(yyjson_mut_get_sint(yyjson_mut_obj_get(data, "default_video_device")), 0);
  ASCIICHAT_REQUIRE_EQ(yyjson_mut_get_sint(yyjson_mut_obj_get(data, "default_audio_device")), 2);
  yyjson_mut_doc_free(response);
  devices_fail = true;
  CLEAR_ERRNO_ALL();
  response = device_list_result(NULL, NULL);
  result = yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "result");
  data = yyjson_mut_obj_get(result, "structuredContent");
  ASCIICHAT_REQUIRE_EQ(yyjson_mut_arr_size(yyjson_mut_obj_get(data, "errors")), 2);
  ASCIICHAT_REQUIRE(yyjson_mut_is_null(yyjson_mut_obj_get(data, "default_video_device")));
  ASCIICHAT_REQUIRE(yyjson_mut_is_null(yyjson_mut_obj_get(data, "default_audio_device")));
  yyjson_mut_doc_free(response);
}

#include "../../../src/mcp-server/downloads.c"

TEST_CASE("mcp_requests/hosted_download_names_are_strict_and_expiration_is_anchored_at_publication", "[mcp_requests]") {
  const char *name = "1700000000-0123456789abcdef0123456789abcdef.png";
  int64_t expiry = 0;
  ASCIICHAT_REQUIRE(mcp_download_name(name, &expiry));
  ASCIICHAT_REQUIRE_EQ(expiry, 1700000000);
  ASCIICHAT_REQUIRE_NOT(mcp_download_name("../1700000000-0123456789abcdef0123456789abcdef.png", NULL));
  ASCIICHAT_REQUIRE_NOT(mcp_download_name("1700000000-0123456789abcdef0123456789abcdeg.png", NULL));
  ASCIICHAT_REQUIRE_NOT(mcp_download_name("1700000000-0123456789abcdef0123456789abcdef.html", NULL));
  ASCIICHAT_REQUIRE_NOT(mcp_download_name("1700000000-0123456789abcdef0123456789abcdef.png/other", NULL));

  char directory[PLATFORM_MAX_PATH_LENGTH], path[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "mcp-download-contract"), ASCIICHAT_OK);
  SAFE_SNPRINTF(path, sizeof(path), "%s/%s", directory, name);
  FILE *f = platform_fopen("download-contract", path, "wb");
  ASCIICHAT_REQUIRE_NOT_NULL(f);
  ASCIICHAT_REQUIRE_EQ(fwrite("image", 1, 5, f), 5);
  ASCIICHAT_REQUIRE_EQ(fclose(f), 0);
  mcp_policy_t policy = {.downloads_url_prefix = "https://example.test/"};
  SAFE_STRNCPY(policy.downloads_dir, directory, sizeof(policy.downloads_dir));
  yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *root = yyjson_mut_obj(doc), *result = yyjson_mut_obj(doc), *data = yyjson_mut_obj(doc),
                 *files = yyjson_mut_arr(doc), *content = yyjson_mut_arr(doc), *item = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);
  yyjson_mut_obj_add_val(doc, root, "result", result);
  yyjson_mut_obj_add_val(doc, result, "structuredContent", data);
  yyjson_mut_obj_add_val(doc, result, "content", content);
  yyjson_mut_arr_add_val(content, item);
  yyjson_mut_obj_add_str(doc, item, "text", "old");
  yyjson_mut_obj_add_val(doc, data, "saved_files", files);
  yyjson_mut_arr_add_strcpy(doc, files, path);
  ASCIICHAT_REQUIRE_EQ(mcp_download_finalize(&policy, doc, 1700000100), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_mut_get_str(yyjson_mut_obj_get(data, "expires_at")), "2023-11-14T22:45:00Z");
  ASCIICHAT_REQUIRE_STR_EQ(yyjson_mut_get_str(yyjson_mut_obj_get(data, "download_url")),
                   "https://example.test/downloads/1700001900-0123456789abcdef0123456789abcdef.png");
  ASCIICHAT_REQUIRE_NE(platform_access(path, PLATFORM_ACCESS_EXISTS), 0);
  SAFE_SNPRINTF(path, sizeof(path), "%s/1700001900-0123456789abcdef0123456789abcdef.png", directory);
  ASCIICHAT_REQUIRE_EQ(platform_access(path, PLATFORM_ACCESS_EXISTS), 0);
  char unrelated[PLATFORM_MAX_PATH_LENGTH];
  SAFE_SNPRINTF(unrelated, sizeof(unrelated), "%s/notes.png", directory);
  f = platform_fopen("unrelated-file", unrelated, "wb");
  ASCIICHAT_REQUIRE_NOT_NULL(f);
  ASCIICHAT_REQUIRE_EQ(fclose(f), 0);
  mcp_download_prune(directory, 1700001899);
  ASCIICHAT_REQUIRE_EQ(platform_access(path, PLATFORM_ACCESS_EXISTS), 0);
  mcp_download_prune(directory, 1700001900);
  ASCIICHAT_REQUIRE_NE(platform_access(path, PLATFORM_ACCESS_EXISTS), 0);
  ASCIICHAT_REQUIRE_EQ(platform_access(unrelated, PLATFORM_ACCESS_EXISTS), 0);
  uv_fs_t unlink_request;
  ASCIICHAT_REQUIRE_EQ(uv_fs_unlink(NULL, &unlink_request, unrelated, NULL), 0);
  uv_fs_req_cleanup(&unlink_request);
  yyjson_mut_doc_free(doc);
  ASCIICHAT_REQUIRE_EQ(platform_rmdir_recursive(directory), ASCIICHAT_OK);
}

TEST_CASE("mcp_requests/hosted_profile_accepts_url_and_stages_requested_media_format", "[mcp_requests]") {
  char directory[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "mcp-hosted-contract"), ASCIICHAT_OK);
  mcp_policy_t policy = {.hosted = true, .restricted = true};
  SAFE_STRNCPY(policy.downloads_dir, directory, sizeof(policy.downloads_dir));
  yyjson_doc *doc =
      read_json("{\"url\":\"https://example.test/"
                "upload.mp4\",\"output_format\":\"mp4\",\"options\":{\"snapshot-delay\":2,\"audio\":true}}");
  http_renderer_process_request_t request;
  ASCIICHAT_REQUIRE_EQ(mcp_request_parse(&policy, MCP_URL, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(has_arg(&request, "--url=https://example.test/upload.mp4"));
  ASCIICHAT_REQUIRE(has_arg(&request, "--audio=true"));
  ASCIICHAT_REQUIRE(strstr(request.stage_file, ".mp4"));
  ASCIICHAT_REQUIRE(mcp_download_name(strrchr(request.output_path, '/') + 1, NULL));
  mcp_request_destroy(&request);
  yyjson_doc_free(doc);
  ASCIICHAT_REQUIRE_EQ(platform_rmdir_recursive(directory), ASCIICHAT_OK);
}

TEST_CASE("mcp_requests/hosted_default_format_tracks_snapshot_mode", "[mcp_requests]") {
  char directory[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "mcp-format-contract"), ASCIICHAT_OK);
  mcp_policy_t policy = {.hosted = true, .restricted = true};
  SAFE_STRNCPY(policy.downloads_dir, directory, sizeof(policy.downloads_dir));
  const char *inputs[] = {"{\"options\":{\"test-pattern\":true}}",
                          "{\"options\":{\"test-pattern\":true,\"snapshot\":false}}",
                          "{\"options\":{\"test-pattern\":true,\"snapshot-delay\":2}}"};
  const char *extensions[] = {".png", ".mp4", ".mp4"};
  for (size_t i = 0; i < 3; ++i) {
    yyjson_doc *doc = read_json(inputs[i]);
    http_renderer_process_request_t request;
    ASCIICHAT_REQUIRE_EQ(mcp_request_parse(&policy, MCP_FILE, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(request.stage_file, extensions[i]));
    mcp_request_destroy(&request);
    yyjson_doc_free(doc);
  }
  ASCIICHAT_REQUIRE_EQ(platform_rmdir_recursive(directory), ASCIICHAT_OK);
}

TEST_CASE("mcp_requests/gif_media_defaults_to_animation_and_respects_snapshot_options", "[mcp_requests]") {
  char directory[PLATFORM_MAX_PATH_LENGTH];
  ASCIICHAT_REQUIRE_EQ(platform_mkdtemp(directory, sizeof(directory), "mcp-gif-contract"), ASCIICHAT_OK);
  mcp_policy_t policy = {.hosted = true, .restricted = true};
  SAFE_STRNCPY(policy.downloads_dir, directory, sizeof(policy.downloads_dir));
  const char *inputs[] = {
      "{\"url\":\"https://example.test/video.mp4\",\"output_format\":\"gif\"}",
      "{\"url\":\"https://example.test/video.mp4\",\"output_format\":\"gif\",\"options\":{\"snapshot\":true}}",
      "{\"url\":\"https://example.test/video.mp4\",\"output_format\":\"gif\",\"options\":{\"snapshot-delay\":2}}"};
  for (size_t i = 0; i < 3; ++i) {
    yyjson_doc *doc = read_json(inputs[i]);
    http_renderer_process_request_t request;
    ASCIICHAT_REQUIRE_EQ(mcp_request_parse(&policy, MCP_URL, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(has_arg(&request, "--snapshot=false"), i == 0);
    mcp_request_destroy(&request);
    yyjson_doc_free(doc);
  }
  ASCIICHAT_REQUIRE_EQ(platform_rmdir_recursive(directory), ASCIICHAT_OK);
}
