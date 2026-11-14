#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/manpage/parser.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
static options_config_t *documentation_config(void) {
  options_builder_t *builder = options_builder_create(sizeof(options_t));
  ASCIICHAT_REQUIRE_NOT_NULL(builder);
  builder->program_name = "ascii-chat";
  builder->description = "Terminal video chat";
  ASCIICHAT_REQUIRE_EQ(options_registry_add_all_to_builder(builder), ASCIICHAT_OK);
  options_builder_add_usage(builder, "server", "[address]", true, "Host a session");
  options_builder_add_usage(builder, NULL, "<session>", false, "Join a session");
  options_builder_add_mode(builder, "server", "Host a session");
  options_builder_add_mode(builder, "client", "Join a session");
  options_builder_add_example(builder, OPTION_MODE_SERVER, "--port 27224", "Host securely", false);
  options_builder_add_example_utility(builder, OPTION_MODE_MIRROR, "cat sample.mp4 | ascii-chat mirror -f -",
                                      "Pipe a video", true);
  options_config_t *config = options_builder_build(builder);
  options_builder_destroy(builder);
  ASCIICHAT_REQUIRE_NOT_NULL(config);
  return config;
}

static char *read_stream(FILE *stream) {
  ASCIICHAT_REQUIRE_EQ(fflush(stream), 0);
  ASCIICHAT_REQUIRE_EQ(fseek(stream, 0, SEEK_END), 0);
  long length = ftell(stream);
  ASCIICHAT_REQUIRE_GE(length, 0);
  ASCIICHAT_REQUIRE_EQ(fseek(stream, 0, SEEK_SET), 0);
  char *text = SAFE_CALLOC((size_t)length + 1, 1, char *);
  ASCIICHAT_REQUIRE_EQ(fread(text, 1, (size_t)length, stream), (size_t)length);
  return text;
}

TEST_CASE("option_documentation/mode_help_exposes_relevant_options_at_narrow_and_wide_widths", "[option_documentation]") {
  options_config_t *config = documentation_config();
  const asciichat_mode_t modes[] = {MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE};
  const char *widths[] = {"45", "120"};
  for (size_t w = 0; w < 2; ++w)
    for (size_t m = 0; m < sizeof(modes) / sizeof(*modes); ++m) {
      platform_setenv("COLUMNS", widths[w]);
      FILE *stream = tmpfile();
      ASCIICHAT_REQUIRE_NOT_NULL(stream);
      options_print_help_for_mode(config, modes[m], "ascii-chat", "Terminal video chat", stream);
      char *text = read_stream(stream);
      ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "Terminal video chat"));
      ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "--help"));
      if (modes[m] == MODE_SERVER)
        ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "--max-clients"));
      if (modes[m] == MODE_CLIENT)
        ASCIICHAT_REQUIRE_NULL(strstr(text, "--max-clients"));
      SAFE_FREE(text);
      fclose(stream);
    }
  options_config_destroy(config);
}

TEST_CASE("option_documentation/usage_and_option_sections_can_be_composed_independently", "[option_documentation]") {
  options_config_t *config = documentation_config();
  FILE *stream = tmpfile();
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  options_config_print_usage(config, stream);
  options_config_print_usage_section(config, stream);
  int width = options_config_calculate_max_col_width(config);
  ASCIICHAT_REQUIRE_GT(width, 0);
  options_config_print_options_sections_with_width(config, stream, width, MODE_SERVER);
  options_config_print_options_sections(config, stream, MODE_CLIENT);
  char *text = read_stream(stream);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "Host a session"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "--port"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "--password"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "--color-mode"));
  SAFE_FREE(text);
  fclose(stream);
  options_config_destroy(config);
}

TEST_CASE("option_documentation/templates_and_merged_manpages_contain_generated_options_and_manual_sections", "[option_documentation]") {
  options_config_t *config = documentation_config();
  char path[1024];
  int fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, sizeof(path), "ascii-manpage", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  const char *modes[] = {NULL, "server", "client", "mirror", "discovery-service"};
  for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
    ASCIICHAT_REQUIRE_EQ(options_config_generate_manpage_template(config, "ascii-chat", modes[i], path, "Terminal chat"),
                 ASCIICHAT_OK);
    FILE *stream = platform_fopen("manpage-test", path, "r");
    ASCIICHAT_REQUIRE_NOT_NULL(stream);
    char *text = read_stream(stream);
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, ".SH"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "OPTIONS"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "port"));
    ASCIICHAT_REQUIRE_NOT_NULL(strstr(text, "password"));
    SAFE_FREE(text);
    fclose(stream);
  }
  ASCIICHAT_REQUIRE_EQ(platform_delete_temp_file(path), 0);
  asciichat_error_t result = options_config_generate_manpage_merged(config, "ascii-chat", NULL, path, "Terminal chat");
  asciichat_error_context_t context{};
  HAS_ERRNO(&context);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "%s", context.context_message);
  size_t count = 0;
  parsed_section_t *sections = parse_manpage_sections(path, &count);
  ASCIICHAT_REQUIRE_NOT_NULL(sections);
  ASCIICHAT_REQUIRE_GT(count, 3);
  ASCIICHAT_REQUIRE_NOT_NULL(find_section(sections, count, "DESCRIPTION"));
  ASCIICHAT_REQUIRE_NOT_NULL(find_section(sections, count, "OPTIONS"));
  free_parsed_sections(sections, count);
  ASCIICHAT_REQUIRE_EQ(platform_delete_temp_file(path), 0);
  options_config_destroy(config);
}

TEST_CASE("option_documentation/parser_preserves_manual_merge_and_unmarked_section_content", "[option_documentation]") {
  const char *input =
      ".TH TEST 1\n.\\\" AUTO-START: OPTIONS\n.SH OPTIONS\nGenerated options\n.\\\" AUTO-END: OPTIONS\n"
      ".\\\" MANUAL-START: DESCRIPTION\n.SH \"DESCRIPTION\"\nKeep this prose\n.\\\" MANUAL-END: DESCRIPTION\n"
      ".\" MERGE-START: ENVIRONMENT\n.SH ENVIRONMENT\nCUSTOM_ENV\n.\" MERGE-END: ENVIRONMENT\n.SH NOTES\nUnmarked "
      "prose\n";
  parsed_section_t *sections = NULL;
  size_t count = 0;
  ASCIICHAT_REQUIRE_EQ(manpage_parser_parse_memory(input, strlen(input), &sections, &count), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(count, 4);
  const parsed_section_t *section = manpage_parser_find_section(sections, count, "DESCRIPTION");
  ASCIICHAT_REQUIRE_NOT_NULL(section);
  ASCIICHAT_REQUIRE_EQ(section->type, SECTION_TYPE_MANUAL);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(section->content, "Keep this prose"));
  section = manpage_parser_find_section(sections, count, "OPTIONS");
  ASCIICHAT_REQUIRE_EQ(section->type, SECTION_TYPE_AUTO);
  section = manpage_parser_find_section(sections, count, "ENVIRONMENT");
  ASCIICHAT_REQUIRE_EQ(section->type, SECTION_TYPE_MERGE);
  section = manpage_parser_find_section(sections, count, "NOTES");
  ASCIICHAT_REQUIRE_EQ(section->type, SECTION_TYPE_UNMARKED);
  ASCIICHAT_REQUIRE_NULL(manpage_parser_find_section(sections, count, "MISSING"));
  manpage_parser_free_sections(sections, count);
  ASCIICHAT_REQUIRE_EQ(manpage_parser_parse_memory(NULL, 1, &sections, &count), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(manpage_parser_parse_memory("", 0, &sections, &count), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(manpage_parser_parse_file(NULL, &sections, &count), ERROR_INVALID_PARAM);
  manpage_parser_free_sections(NULL, 0);
}

TEST_CASE("option_documentation/mode_names_and_groff_escaping_are_stable", "[option_documentation]") {
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(format_mode_names(static_cast<option_mode_bitmask_t>(OPTION_MODE_SERVER | OPTION_MODE_CLIENT)), "server"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(format_mode_names(static_cast<option_mode_bitmask_t>(OPTION_MODE_BINARY | OPTION_MODE_MIRROR)), "global"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(format_mode_names(OPTION_MODE_DISCOVERY_SVC), "discovery-service"));
  ASCIICHAT_REQUIRE_STR_EQ(format_mode_names(OPTION_MODE_MCP_SERVER), "mcp-server");
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(format_mode_names(static_cast<option_mode_bitmask_t>(OPTION_MODE_BINARY | OPTION_MODE_MCP_SERVER)), "mcp-server"));
  ASCIICHAT_REQUIRE_STR_EQ(escape_groff_special("--file"), "--file");
  ASCIICHAT_REQUIRE_STR_EQ(escape_groff_special(NULL), "");
  options_config_t *config = documentation_config();
  ASCIICHAT_REQUIRE_EQ(options_config_generate_manpage_template(NULL, "x", NULL, NULL, "x"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(options_config_generate_manpage_template(config, NULL, NULL, NULL, "x"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(options_config_generate_manpage_merged(NULL, "x", NULL, NULL, "x"), ERROR_INVALID_PARAM);
  options_config_destroy(config);
}
