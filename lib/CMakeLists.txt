# lib/ - Core application infrastructure
# Sources from: log/, logging/, options/, discovery/, debug/, ui/, session/

set(CORE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/asciichat_errno.c
    ${CMAKE_CURRENT_SOURCE_DIR}/embedded_resources.c
    # log/
    ${CMAKE_CURRENT_SOURCE_DIR}/log/logging.c
    ${CMAKE_CURRENT_SOURCE_DIR}/log/colorize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/log/mmap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/log/grep.c
    ${CMAKE_CURRENT_SOURCE_DIR}/log/interactive_grep.c
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/file_parser.c
    # platform/terminal.c - unified color detection
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/terminal.c
    # ui/
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/terminal_screen.c
    # options/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/colorscheme.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/options.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/enums.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/strings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/levenshtein.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/rcu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/layout.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/help_api.c
    # options/builder/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/builder/builder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/builder/handlers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/builder/help.c
    # options/config/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/config/config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/config/schema.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/config/presets.c
    # options/registry/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/metadata.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/general.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/logging.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/configuration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/terminal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/webcam.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/network.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/security.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/media.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/database.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/public_api.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/registry/mode_defaults.c
    # options/parsing/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/parsing/parsers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/parsing/validation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/parsing/actions.c
    # options/manpage/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/resources.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/formatter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/merger.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/content/options.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/content/environment.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/content/usage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/content/examples.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/manpage/content/positional.c
    # options/completions/
    ${CMAKE_CURRENT_SOURCE_DIR}/options/completions/completions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/completions/bash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/completions/fish.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/completions/zsh.c
    ${CMAKE_CURRENT_SOURCE_DIR}/options/completions/powershell.c
    # version
    ${CMAKE_CURRENT_SOURCE_DIR}/version.c
    # discovery/ (library)
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/session.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/database.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/identity.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/strings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/adjectives.c
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery/nouns.c
    # src/discovery/ (client code that's part of lib)
    ${CMAKE_SOURCE_DIR}/src/discovery/nat.c
    ${CMAKE_SOURCE_DIR}/src/discovery/negotiate.c
    ${CMAKE_SOURCE_DIR}/src/discovery/session.c
    ${CMAKE_SOURCE_DIR}/src/discovery/webrtc.c
    # Third-party: tomlc17
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c
)

# Debug sources only in non-Release builds
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    list(APPEND CORE_SRCS
        ${CMAKE_CURRENT_SOURCE_DIR}/debug/lock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/debug/memory.c
    )
endif()

# Third-party code properties (tomlc17)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/tomlc17/src/tomlc17.c
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    SKIP_LINTING ON
    COMPILE_FLAGS "-fno-sanitize=implicit-conversion"
    DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Session library
set(SESSION_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/session/consensus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/consensus_integration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/capture.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/render.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/participant.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/keyboard_handler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/session/session_log_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/help_screen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/splash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/server_status.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/discovery_status.c
)

set(CORE_SRCS ${CORE_SRCS} PARENT_SCOPE)
set(SESSION_SRCS ${SESSION_SRCS} PARENT_SCOPE)
