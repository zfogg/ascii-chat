#include <criterion/criterion.h>

#include <ascii-chat/options/config.h>
#include <ascii-chat/options/options.h>
#include <libflags/manpage/manpage.h>

#include <string.h>

TestSuite(option_documentation);

Test(option_documentation, libflags_schema_writes_options_and_environment) {
  flags_schema_builder_t *builder = options_config_build_flags_schema(OPTION_MODE_ALL);
  cr_assert_not_null(builder);
  FILE *stream = tmpfile();
  cr_assert_not_null(stream);
  const flags_manpage_metadata_t metadata = {
      .program_name = "ascii-chat",
      .section = "1",
      .description = "Video chat in your terminal",
  };
  cr_assert(flags_manpage_write(flags_schema_builder_schema(builder), &metadata, stream));
  cr_assert(flags_manpage_write_environment(flags_schema_builder_schema(builder), "ASCII_CHAT_", stream));
  cr_assert_eq(fseek(stream, 0, SEEK_SET), 0);
  char output[8192] = {0};
  (void)fread(output, 1, sizeof(output) - 1, stream);
  cr_assert_not_null(strstr(output, "OPTIONS"));
  cr_assert_not_null(strstr(output, "port"));
  cr_assert_not_null(strstr(output, "ENVIRONMENT"));
  fclose(stream);
  flags_schema_builder_destroy(builder);
}
