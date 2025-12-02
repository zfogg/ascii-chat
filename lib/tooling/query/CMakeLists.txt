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

# Runtime library for QUERY_INIT/QUERY_SHUTDOWN macros
add_library(ascii-query-runtime STATIC query.c)

# Include from lib/ so users can do: #include "tooling/query/query.h"
target_include_directories(ascii-query-runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lib>
    $<INSTALL_INTERFACE:include>
)

# Add compile definition to indicate query support is available
target_compile_definitions(ascii-query-runtime PUBLIC
    ASCIICHAT_QUERY_ENABLED=1
)
