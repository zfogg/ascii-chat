# CRC implementations have function-local target attributes and runtime probes.
# Never enable CRC instructions globally, including on the build host.
set(ASCIICHAT_CRC32_HW "auto" CACHE STRING "CRC32 hardware acceleration: auto, on, off")
if(NOT ASCIICHAT_CRC32_HW MATCHES "^(auto|on|off)$")
    message(FATAL_ERROR "Invalid ASCIICHAT_CRC32_HW: ${ASCIICHAT_CRC32_HW}")
endif()
set(ASCIICHAT_ENABLE_CRC32_HW FALSE)
if(NOT ASCIICHAT_CRC32_HW STREQUAL "off" AND (ASCIICHAT_IS_X86_64 OR ASCIICHAT_IS_ARM64))
    set(ASCIICHAT_ENABLE_CRC32_HW TRUE)
    add_compile_definitions(HAVE_CRC32_HW)
endif()
