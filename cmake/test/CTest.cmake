set(CTEST_PROJECT_NAME "ascii-chat")
set(CTEST_NIGHTLY_START_TIME "01:00:00 UTC")

# Developers can override the dashboard server by setting
#   -DCTEST_DROP_SITE=...
# or by exporting DASHBOARD_SERVER in the environment.

if(DEFINED ENV{ASCII_CHAT_CTEST_DROP_SITE})
    set(CTEST_DROP_SITE "$ENV{ASCII_CHAT_CTEST_DROP_SITE}")
endif()

if(NOT CTEST_DROP_SITE)
    set(CTEST_DROP_SITE "dashboard.example.com")
endif()

set(CTEST_DROP_SITE_CDASH TRUE)
set(CTEST_DROP_LOCATION "/submit.php?project=ascii-chat")
set(CTEST_DROP_METHOD "http")

