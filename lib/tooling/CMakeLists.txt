# lib/tooling - Panic instrumentation runtime

set(TOOLING_PANIC_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/panic/instrument_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/panic/instrument_cov.c
)

set(TOOLING_PANIC_REPORT_SRCS
    ${CMAKE_SOURCE_DIR}/src/tooling/panic/report.c
)

set(TOOLING_PANIC_SRCS ${TOOLING_PANIC_SRCS} PARENT_SCOPE)
set(TOOLING_PANIC_REPORT_SRCS ${TOOLING_PANIC_REPORT_SRCS} PARENT_SCOPE)
