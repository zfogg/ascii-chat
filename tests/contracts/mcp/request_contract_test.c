#include <criterion/criterion.h>
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
static int fixture_worker(asciichat_thread_t *thread, const char *name, void *(*fn)(void *), void *data) {
  (void)thread;
  (void)name;
  (void)fn;
  (void)data;
  return -1;
}
void mcp_render_execute(mcp_render_request_t *request, atomic_t *cancelled, mcp_render_result_t *result) {
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
#define acthread_create fixture_worker
#include "../../../src/mcp-server/protocol.c"
#undef webcam_list_devices
#undef audio_list_input_devices
#undef webcam_free_device_list
#undef audio_free_device_list
#undef acthread_create
static void setup(void) {
  log_init(NULL, LOG_DEV, false, false);
  log_set_terminal_output(false);
  errno_suppress(true);
  cr_assert_eq(options_state_init(), ASCIICHAT_OK);
  registry_init_size();
}
static void teardown(void) {
  options_state_destroy();
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(mcp_requests, .init = setup, .fini = teardown, .timeout = 20);
static bool has_arg(const mcp_render_request_t *request, const char *value) {
  for (size_t i = 0; i < request->argc; ++i)
    if (!strcmp(request->argv[i], value))
      return true;
  return false;
}
static yyjson_doc *read_json(const char *text) {
  yyjson_doc *doc = yyjson_read(text, strlen(text), 0);
  cr_assert_not_null(doc);
  return doc;
}
Test(mcp_requests, malformed_arguments_and_incompatible_sources_are_rejected_and_partial_allocations_are_released) {
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
    mcp_render_request_t request;
    cr_assert_neq(mcp_request_parse(&policy, cases[i].tool, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK,
                  "case %u: %s", i, cases[i].json);
    mcp_request_destroy(&request);
    cr_assert_eq(request.argc, 0);
    cr_assert_null(request.argv);
    cr_assert_eq(request.upload_dir[0], 0);
    yyjson_doc_free(doc);
    CLEAR_ERRNO_ALL();
  }
}

Test(mcp_requests, accepted_sources_produce_bounded_cli_arguments_and_staged_uploads_are_destroyed) {
  const struct {
    mcp_tool_t tool;
    const char *json;
    const char *expected;
  } cases[] = {
      {MCP_FILE, "{\"path\":\"local.ppm\"}", "--file=local.ppm"},
      {MCP_FILE, "{\"data_base64\":\"AA==\",\"mime_type\":\"image/png\"}", "--audio-capture-source=media"},
      {MCP_FILE, "{\"options\":{\"test-audio\":true}}", "--waveform=true"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true},\"timeout_ms\":1000}", "--test-pattern=0"},
      {MCP_FILE, "{\"options\":{\"test-pattern\":true,\"renderer\":\"glyph\",\"glyph-enhance\":true}}",
       "--glyph-enhance=true"},
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
    mcp_render_request_t request;
    cr_assert_eq(mcp_request_parse(&policy, cases[i].tool, yyjson_doc_get_root(doc), &request), ASCIICHAT_OK,
                 "case %u: %s", i, cases[i].json);
    cr_assert(has_arg(&request, cases[i].expected), "case %u missing %s", i, cases[i].expected);
    cr_assert_null(request.argv[request.argc]);
    char upload[PLATFORM_MAX_PATH_LENGTH];
    SAFE_STRNCPY(upload, request.upload_dir, sizeof(upload));
    if (upload[0]) {
      char path[PLATFORM_MAX_PATH_LENGTH];
      SAFE_SNPRINTF(path, sizeof(path), "%s/media", upload);
      FILE *f = platform_fopen("uploaded-byte", path, "rb");
      cr_assert_not_null(f);
      cr_assert_eq(fgetc(f), 0);
      cr_assert_eq(fgetc(f), EOF);
      fclose(f);
    }
    mcp_request_destroy(&request);
    if (upload[0])
      cr_assert_neq(platform_access(upload, PLATFORM_ACCESS_EXISTS), 0);
    yyjson_doc_free(doc);
    CLEAR_ERRNO_ALL();
  }
}
Test(mcp_requests, binary_options_are_placed_before_mode_and_argument_overflow_preserves_existing_arguments) {
  mcp_render_request_t request = {.argv = SAFE_CALLOC(MCP_REQUEST_ARGV_CAPACITY, sizeof(char *), char **)};
  cr_assert_eq(arg(&request, "ascii-chat"), ASCIICHAT_OK);
  cr_assert_eq(arg(&request, "mirror"), ASCIICHAT_OK);
  cr_assert_eq(option(&request, "log-level", "debug"), ASCIICHAT_OK);
  cr_assert_str_eq(request.argv[1], "--log-level=debug");
  cr_assert_str_eq(request.argv[2], "mirror");
  while (request.argc < MCP_REQUEST_ARGV_CAPACITY - MCP_REQUEST_RESERVED_ARGS)
    cr_assert_eq(arg(&request, "--snapshot=true"), ASCIICHAT_OK);
  size_t count = request.argc;
  cr_assert_eq(arg(&request, "overflow"), ERROR_INVALID_PARAM);
  cr_assert_eq(request.argc, count);
  cr_assert_null(request.argv[count]);
  mcp_request_destroy(&request);
}
Test(mcp_requests, output_staging_does_not_create_or_replace_the_published_destination) {
  char directory[PLATFORM_MAX_PATH_LENGTH];
  cr_assert_eq(platform_mkdtemp(directory, sizeof(directory), "mcp-stage-contract"), ASCIICHAT_OK);
  char output[PLATFORM_MAX_PATH_LENGTH];
  SAFE_SNPRINTF(output, sizeof(output), "%s/picture.png", directory);
  mcp_render_request_t request = {.argv = SAFE_CALLOC(MCP_REQUEST_ARGV_CAPACITY, sizeof(char *), char **)};
  cr_assert_eq(stage_output(output, &request), ASCIICHAT_OK);
  cr_assert_neq(platform_access(output, PLATFORM_ACCESS_EXISTS), 0);
  cr_assert_eq(platform_access(request.stage_dir, PLATFORM_ACCESS_EXISTS), 0);
  char stage[PLATFORM_MAX_PATH_LENGTH];
  SAFE_STRNCPY(stage, request.stage_dir, sizeof(stage));
  mcp_request_destroy(&request);
  cr_assert_neq(platform_access(stage, PLATFORM_ACCESS_EXISTS), 0);
  FILE *f = platform_fopen("existing-output", output, "wb");
  cr_assert_not_null(f);
  cr_assert_eq(fwrite("keep", 1, 4, f), 4);
  cr_assert_eq(fclose(f), 0);
  request = (mcp_render_request_t){.argv = SAFE_CALLOC(MCP_REQUEST_ARGV_CAPACITY, sizeof(char *), char **)};
  cr_assert_eq(stage_output(output, &request), ERROR_INVALID_PARAM);
  mcp_request_destroy(&request);
  f = platform_fopen("preserved-output", output, "rb");
  cr_assert_not_null(f);
  char bytes[4];
  cr_assert_eq(fread(bytes, 1, 4, f), 4);
  cr_assert_arr_eq(bytes, "keep", 4);
  fclose(f);
  cr_assert_eq(platform_unlink(output), 0);
  cr_assert_eq(platform_rmdir_recursive(directory), ASCIICHAT_OK);
}
static yyjson_mut_doc *dispatch_text(mcp_server_t *server, const char *json, void *connection) {
  yyjson_doc *doc = read_json(json);
  bool pending = true;
  yyjson_mut_doc *response = mcp_dispatch(server, doc, connection, NULL, &pending);
  cr_assert_not(pending);
  yyjson_doc_free(doc);
  return response;
}
static void expect_rpc_error(mcp_server_t *server, const char *json, int code) {
  yyjson_mut_doc *response = dispatch_text(server, json, NULL);
  cr_assert_not_null(response);
  yyjson_mut_val *error = yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "error");
  cr_assert_not_null(error);
  cr_assert_eq(yyjson_mut_get_sint(yyjson_mut_obj_get(error, "code")), code);
  yyjson_mut_doc_free(response);
  CLEAR_ERRNO_ALL();
}

Test(mcp_requests, protocol_envelopes_reject_invalid_types_and_worker_start_failure_releases_job) {
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
  cr_assert_not(server.jobs[0].started);
  cr_assert_null(server.jobs[0].message);
  cr_assert_null(server.jobs[0].request.argv);
}

Test(mcp_requests, device_results_include_defaults_and_report_partial_enumeration_failure) {
  yyjson_mut_doc *response = device_list_result(NULL, NULL);
  yyjson_mut_val *root = yyjson_mut_doc_get_root(response), *result = yyjson_mut_obj_get(root, "result"),
                 *data = yyjson_mut_obj_get(result, "structuredContent");
  cr_assert_not_null(data);
  cr_assert_eq(yyjson_mut_arr_size(yyjson_mut_obj_get(data, "video_devices")), 2);
  cr_assert_eq(yyjson_mut_arr_size(yyjson_mut_obj_get(data, "audio_devices")), 2);
  cr_assert_eq(yyjson_mut_get_sint(yyjson_mut_obj_get(data, "default_video_device")), 0);
  cr_assert_eq(yyjson_mut_get_sint(yyjson_mut_obj_get(data, "default_audio_device")), 2);
  yyjson_mut_doc_free(response);
  devices_fail = true;
  CLEAR_ERRNO_ALL();
  response = device_list_result(NULL, NULL);
  result = yyjson_mut_obj_get(yyjson_mut_doc_get_root(response), "result");
  data = yyjson_mut_obj_get(result, "structuredContent");
  cr_assert_eq(yyjson_mut_arr_size(yyjson_mut_obj_get(data, "errors")), 2);
  cr_assert(yyjson_mut_is_null(yyjson_mut_obj_get(data, "default_video_device")));
  cr_assert(yyjson_mut_is_null(yyjson_mut_obj_get(data, "default_audio_device")));
  yyjson_mut_doc_free(response);
}
