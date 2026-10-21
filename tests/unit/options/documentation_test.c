#include <criterion/criterion.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/manpage.h>
#include <ascii-chat/options/manpage/parser.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(option_documentation);

static options_config_t *documentation_config(void) {
  options_builder_t *builder = options_builder_create(sizeof(options_t));
  cr_assert_not_null(builder);
  builder->program_name = "ascii-chat";
  builder->description = "Terminal video chat";
  cr_assert_eq(options_registry_add_all_to_builder(builder), ASCIICHAT_OK);
  options_builder_add_usage(builder, "server", "[address]", true, "Host a session");
  options_builder_add_usage(builder, NULL, "<session>", false, "Join a session");
  options_builder_add_mode(builder, "server", "Host a session");
  options_builder_add_mode(builder, "client", "Join a session");
  options_builder_add_example(builder, OPTION_MODE_SERVER, "--port 27224", "Host securely", false);
  options_builder_add_example_utility(builder, OPTION_MODE_MIRROR, "cat sample.mp4 | ascii-chat mirror -f -",
                                      "Pipe a video", true);
  options_config_t *config = options_builder_build(builder);
  options_builder_destroy(builder);
  cr_assert_not_null(config);
  return config;
}

static char *read_stream(FILE *stream) {
  cr_assert_eq(fflush(stream), 0);
  cr_assert_eq(fseek(stream, 0, SEEK_END), 0);
  long length = ftell(stream);
  cr_assert_geq(length, 0);
  cr_assert_eq(fseek(stream, 0, SEEK_SET), 0);
  char *text = SAFE_CALLOC((size_t)length + 1, 1, char *);
  cr_assert_eq(fread(text, 1, (size_t)length, stream), (size_t)length);
  return text;
}

Test(option_documentation, mode_help_exposes_relevant_options_at_narrow_and_wide_widths) {
  options_config_t *config = documentation_config();
  const asciichat_mode_t modes[] = {MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE};
  const char *widths[] = {"45", "120"};
  for (size_t w = 0; w < 2; ++w)
    for (size_t m = 0; m < sizeof(modes) / sizeof(*modes); ++m) {
      platform_setenv("COLUMNS", widths[w]);
      FILE *stream = tmpfile();
      cr_assert_not_null(stream);
      options_print_help_for_mode(config, modes[m], "ascii-chat", "Terminal video chat", stream);
      char *text = read_stream(stream);
      cr_assert_not_null(strstr(text, "Terminal video chat"));
      cr_assert_not_null(strstr(text, "--help"));
      if (modes[m] == MODE_SERVER)
        cr_assert_not_null(strstr(text, "--max-clients"));
      if (modes[m] == MODE_CLIENT)
        cr_assert_null(strstr(text, "--max-clients"));
      SAFE_FREE(text);
      fclose(stream);
    }
  options_config_destroy(config);
}

Test(option_documentation, usage_and_option_sections_can_be_composed_independently) {
  options_config_t *config = documentation_config();
  FILE *stream = tmpfile();
  cr_assert_not_null(stream);
  options_config_print_usage(config, stream);
  options_config_print_usage_section(config, stream);
  int width = options_config_calculate_max_col_width(config);
  cr_assert_gt(width, 0);
  options_config_print_options_sections_with_width(config, stream, width, MODE_SERVER);
  options_config_print_options_sections(config, stream, MODE_CLIENT);
  char *text = read_stream(stream);
  cr_assert_not_null(strstr(text, "Host a session"));
  cr_assert_not_null(strstr(text, "--port"));
  cr_assert_not_null(strstr(text, "--password"));
  cr_assert_not_null(strstr(text, "--color-mode"));
  SAFE_FREE(text);
  fclose(stream);
  options_config_destroy(config);
}

Test(option_documentation, templates_and_merged_manpages_contain_generated_options_and_manual_sections) {
  options_config_t *config = documentation_config();
  char path[1024];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-manpage", &fd), 0);
  if (fd >= 0)
    platform_close(fd);
  const char *modes[] = {NULL, "server", "client", "mirror", "discovery-service"};
  for (size_t i = 0; i < sizeof(modes) / sizeof(*modes); ++i) {
    cr_assert_eq(options_config_generate_manpage_template(config, "ascii-chat", modes[i], path, "Terminal chat"),
                 ASCIICHAT_OK);
    FILE *stream = platform_fopen("manpage-test", path, "r");
    cr_assert_not_null(stream);
    char *text = read_stream(stream);
    cr_assert_not_null(strstr(text, ".SH"));
    cr_assert_not_null(strstr(text, "OPTIONS"));
    cr_assert_not_null(strstr(text, "port"));
    cr_assert_not_null(strstr(text, "password"));
    SAFE_FREE(text);
    fclose(stream);
  }
  cr_assert_eq(platform_delete_temp_file(path), 0);
  asciichat_error_t result = options_config_generate_manpage_merged(config, "ascii-chat", NULL, path, "Terminal chat");
  asciichat_error_context_t context = {0};
  HAS_ERRNO(&context);
  cr_assert_eq(result, ASCIICHAT_OK, "%s", context.context_message);
  size_t count = 0;
  parsed_section_t *sections = parse_manpage_sections(path, &count);
  cr_assert_not_null(sections);
  cr_assert_gt(count, 3);
  cr_assert_not_null(find_section(sections, count, "DESCRIPTION"));
  cr_assert_not_null(find_section(sections, count, "OPTIONS"));
  free_parsed_sections(sections, count);
  cr_assert_eq(platform_delete_temp_file(path), 0);
  options_config_destroy(config);
}

Test(option_documentation, parser_preserves_manual_merge_and_unmarked_section_content) {
  const char *input =
      ".TH TEST 1\n.\\\" AUTO-START: OPTIONS\n.SH OPTIONS\nGenerated options\n.\\\" AUTO-END: OPTIONS\n"
      ".\\\" MANUAL-START: DESCRIPTION\n.SH \"DESCRIPTION\"\nKeep this prose\n.\\\" MANUAL-END: DESCRIPTION\n"
      ".\" MERGE-START: ENVIRONMENT\n.SH ENVIRONMENT\nCUSTOM_ENV\n.\" MERGE-END: ENVIRONMENT\n.SH NOTES\nUnmarked "
      "prose\n";
  parsed_section_t *sections = NULL;
  size_t count = 0;
  cr_assert_eq(manpage_parser_parse_memory(input, strlen(input), &sections, &count), ASCIICHAT_OK);
  cr_assert_eq(count, 4);
  const parsed_section_t *section = manpage_parser_find_section(sections, count, "DESCRIPTION");
  cr_assert_not_null(section);
  cr_assert_eq(section->type, SECTION_TYPE_MANUAL);
  cr_assert_not_null(strstr(section->content, "Keep this prose"));
  section = manpage_parser_find_section(sections, count, "OPTIONS");
  cr_assert_eq(section->type, SECTION_TYPE_AUTO);
  section = manpage_parser_find_section(sections, count, "ENVIRONMENT");
  cr_assert_eq(section->type, SECTION_TYPE_MERGE);
  section = manpage_parser_find_section(sections, count, "NOTES");
  cr_assert_eq(section->type, SECTION_TYPE_UNMARKED);
  cr_assert_null(manpage_parser_find_section(sections, count, "MISSING"));
  manpage_parser_free_sections(sections, count);
  cr_assert_eq(manpage_parser_parse_memory(NULL, 1, &sections, &count), ERROR_INVALID_PARAM);
  cr_assert_eq(manpage_parser_parse_memory("", 0, &sections, &count), ERROR_INVALID_PARAM);
  cr_assert_eq(manpage_parser_parse_file(NULL, &sections, &count), ERROR_INVALID_PARAM);
  manpage_parser_free_sections(NULL, 0);
}

Test(option_documentation, mode_names_and_groff_escaping_are_stable) {
  cr_assert_not_null(strstr(format_mode_names(OPTION_MODE_SERVER | OPTION_MODE_CLIENT), "server"));
  cr_assert_not_null(strstr(format_mode_names(OPTION_MODE_BINARY | OPTION_MODE_MIRROR), "global"));
  cr_assert_not_null(strstr(format_mode_names(OPTION_MODE_DISCOVERY_SVC), "discovery-service"));
  cr_assert_str_eq(escape_groff_special("--file"), "--file");
  cr_assert_str_eq(escape_groff_special(NULL), "");
  options_config_t *config = documentation_config();
  cr_assert_eq(options_config_generate_manpage_template(NULL, "x", NULL, NULL, "x"), ERROR_INVALID_PARAM);
  cr_assert_eq(options_config_generate_manpage_template(config, NULL, NULL, NULL, "x"), ERROR_INVALID_PARAM);
  cr_assert_eq(options_config_generate_manpage_merged(NULL, "x", NULL, NULL, "x"), ERROR_INVALID_PARAM);
  options_config_destroy(config);
}
