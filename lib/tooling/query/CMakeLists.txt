# Query Tool Runtime Library
#
# This library provides the QUERY_INIT/QUERY_SHUTDOWN macros that allow
# applications to auto-spawn the query controller process. The controller
# is a separate process that attaches via LLDB and serves HTTP requests.
#
# Usage in application code:
#   #include "tooling/query/query.h"
#   QUERY_INIT(9999);
#   // ... app runs, can curl localhost:9999/query?... ...
#   QUERY_SHUTDOWN();
#
# This library is only built in Debug mode when ASCIICHAT_BUILD_WITH_QUERY=ON

# Source file will be created in a later commit (step 6.1)
# For now, create an INTERFACE library with just the header
add_library(ascii-query-runtime INTERFACE)

target_include_directories(ascii-query-runtime INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/tooling/query>
)

# When query.c is implemented, change to:
# add_library(ascii-query-runtime STATIC query.c)
# target_include_directories(ascii-query-runtime PUBLIC
#     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
#     $<INSTALL_INTERFACE:include/tooling/query>
# )
# target_compile_definitions(ascii-query-runtime PRIVATE
#     ASCIICHAT_QUERY_ENABLED=1
# )

# Add compile definition to indicate query support is available
target_compile_definitions(ascii-query-runtime INTERFACE
    ASCIICHAT_QUERY_ENABLED=1
)
