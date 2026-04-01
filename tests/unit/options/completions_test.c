/**
 * @file completions_test.c
 * @brief Unit tests for shell completion generation (bash, fish, zsh)
 * @ingroup options
 *
 * Tests verify that:
 * - Enum values appear in completion output
 * - Boolean options have true/false values
 * - Action options have no value completion
 * - Device index helpers are present and functional
 * - zstyle configuration is present
 * - Generated zsh script passes syntax validation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ascii-chat/options/completions/zsh.h>

/**
 * Helper: Generate zsh completion to a temporary file and return contents
 */
static char *generate_zsh_completion(void) {
  char temp_path[] = "/tmp/ascii-chat-zsh-XXXXXX";
  int fd = mkstemp(temp_path);
  cr_assert(fd >= 0, "Failed to create temporary file");

  FILE *output = fdopen(fd, "w");
  cr_assert_not_null(output, "Failed to open temp file for writing");

  asciichat_error_t result = completions_generate_zsh(output);
  cr_assert_eq(result, ASCIICHAT_OK, "Failed to generate zsh completion");

  fclose(output);

  // Read the generated file
  FILE *input = fopen(temp_path, "r");
  cr_assert_not_null(input, "Failed to open temp file for reading");

  fseek(input, 0, SEEK_END);
  long size = ftell(input);
  fseek(input, 0, SEEK_SET);

  char *contents = malloc(size + 1);
  cr_assert_not_null(contents, "Failed to allocate memory for completion contents");

  size_t read = fread(contents, 1, size, input);
  cr_assert_eq(read, (size_t)size, "Failed to read entire completion file");
  contents[size] = '\0';

  fclose(input);
  unlink(temp_path);

  return contents;
}

/**
 * Helper: Check if a string contains a substring
 */
static bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != NULL;
}

/**
 * Helper: Run zsh syntax check on generated script
 */
static int validate_zsh_syntax(const char *completion_script) {
  char temp_path[] = "/tmp/ascii-chat-zsh-syntax-XXXXXX";
  int fd = mkstemp(temp_path);
  if (fd < 0) return -1;

  FILE *f = fdopen(fd, "w");
  if (!f) return -1;

  fwrite(completion_script, 1, strlen(completion_script), f);
  fclose(f);

  // Run: zsh -n <file> (syntax check without execution)
  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    execlp("zsh", "zsh", "-n", temp_path, NULL);
    exit(1);
  }

  int status = 0;
  waitpid(pid, &status, 0);
  unlink(temp_path);

  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

// ============================================================================
// Test Suite: Enum Value Completions
// ============================================================================

Test(zsh_completions, enum_color_mode_values) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "auto none 16 256 truecolor"),
            "color-mode completion missing enum values");
  free(completion);
}

Test(zsh_completions, enum_palette_values) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "standard blocks digital minimal cool custom"),
            "palette completion missing enum values");
  free(completion);
}

Test(zsh_completions, enum_render_mode_values) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "foreground fg background bg half-block"),
            "render-mode completion missing enum values");
  free(completion);
}

Test(zsh_completions, enum_color_filter_values) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "none black white green magenta fuchsia orange teal cyan pink red yellow"),
            "color-filter completion missing enum values");
  free(completion);
}

Test(zsh_completions, enum_audio_source_values) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "auto mic media both"),
            "audio-source completion missing enum values");
  free(completion);
}

Test(zsh_completions, enum_log_level_values) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "dev debug info warn error fatal"),
            "log-level completion missing enum values");
  free(completion);
}

// ============================================================================
// Test Suite: Boolean Option Values
// ============================================================================

Test(zsh_completions, boolean_option_true_false) {
  char *completion = generate_zsh_completion();
  // Check that at least one boolean option has true/false values
  cr_assert(contains(completion, "_values 'value' true false"),
            "boolean options missing true/false completion");
  free(completion);
}

Test(zsh_completions, audio_boolean_completion) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "--audio") && contains(completion, "true false"),
            "--audio boolean option missing true/false values");
  free(completion);
}

// ============================================================================
// Test Suite: Device Index Helpers
// ============================================================================

Test(zsh_completions, webcam_indices_helper_present) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "_ascii_chat_webcam_indices()"),
            "webcam indices helper function missing");
  free(completion);
}

Test(zsh_completions, microphone_indices_helper_present) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "_ascii_chat_microphone_indices()"),
            "microphone indices helper function missing");
  free(completion);
}

Test(zsh_completions, speakers_indices_helper_present) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "_ascii_chat_speakers_indices()"),
            "speakers indices helper function missing");
  free(completion);
}

Test(zsh_completions, webcam_index_option_wired) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "--webcam-index") &&
            contains(completion, "_ascii_chat_webcam_indices"),
            "webcam-index option not wired to helper");
  free(completion);
}

Test(zsh_completions, microphone_index_option_wired) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "--microphone-index") &&
            contains(completion, "_ascii_chat_microphone_indices"),
            "microphone-index option not wired to helper");
  free(completion);
}

Test(zsh_completions, speakers_index_option_wired) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "--speakers-index") &&
            contains(completion, "_ascii_chat_speakers_indices"),
            "speakers-index option not wired to helper");
  free(completion);
}

// ============================================================================
// Test Suite: Zstyle Configuration
// ============================================================================

Test(zsh_completions, zstyle_completer_config_present) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "zstyle ':completion:*:ascii-chat:*' completer"),
            "zstyle completer configuration missing");
  free(completion);
}

Test(zsh_completions, zstyle_approximate_config_present) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "zstyle ':completion:*:ascii-chat:*:approximate:*' max-errors"),
            "zstyle approximate configuration missing");
  free(completion);
}

// ============================================================================
// Test Suite: Syntax Validation
// ============================================================================

Test(zsh_completions, zsh_syntax_validation) {
  char *completion = generate_zsh_completion();
  int syntax_status = validate_zsh_syntax(completion);
  cr_assert(syntax_status == 0, "Generated zsh script has syntax errors");
  free(completion);
}

// ============================================================================
// Test Suite: Case Statement Structure
// ============================================================================

Test(zsh_completions, case_statement_for_option_names) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "case \"$prev\" in"),
            "Case statement for option name matching missing");
  free(completion);
}

Test(zsh_completions, case_statement_for_equals_form) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "case \"${words[CURRENT]}\" in"),
            "Case statement for --option=VALUE form missing");
  free(completion);
}

// ============================================================================
// Test Suite: Grouped Options
// ============================================================================

Test(zsh_completions, describe_grouping_present) {
  char *completion = generate_zsh_completion();
  cr_assert(contains(completion, "_describe"),
            "_describe command for grouped options missing");
  free(completion);
}

Test(zsh_completions, option_groups_exist) {
  char *completion = generate_zsh_completion();
  // Check for at least some common option groups
  cr_assert(contains(completion, "audio") ||
            contains(completion, "AUDIO") ||
            contains(completion, "display") ||
            contains(completion, "DISPLAY"),
            "Option groups/categories missing");
  free(completion);
}

// ============================================================================
// Test Suite: Action Options (Should Have No Value Completion)
// ============================================================================

Test(zsh_completions, action_options_no_values) {
  char *completion = generate_zsh_completion();
  // --help is an action option - it should NOT have value completion
  // We check that --help doesn't appear in the value completion case blocks
  // (it will appear in the _describe grouping, but not in case statements)
  const char *after_prev_case = strstr(completion, "case \"$prev\" in");
  cr_assert_not_null(after_prev_case, "No case statement found");

  const char *before_esac = strstr(after_prev_case, "esac");
  cr_assert_not_null(before_esac, "No esac found");

  // Extract the case block content
  size_t block_size = before_esac - after_prev_case;
  char case_block[4096];
  strncpy(case_block, after_prev_case, block_size < sizeof(case_block) ? block_size : sizeof(case_block) - 1);
  case_block[block_size] = '\0';

  // --help should NOT be in the case block (it's an action option)
  // This test verifies action options are properly excluded
  cr_assert_not(contains(case_block, "--help"),
            "--help action option should not have value completion");

  free(completion);
}
