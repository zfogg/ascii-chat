# Install libvterm static library and headers

file(MAKE_DIRECTORY "${PREFIX}/lib" "${PREFIX}/include")
file(COPY "${SOURCE_DIR}/libvterm.a" DESTINATION "${PREFIX}/lib")
file(GLOB HEADERS "${SOURCE_DIR}/include/vterm*.h")
file(COPY ${HEADERS} DESTINATION "${PREFIX}/include")

message(STATUS "Installed libvterm to ${PREFIX}")
